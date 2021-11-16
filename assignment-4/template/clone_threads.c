#include <clone_threads.h>
#include <context.h>
#include <entry.h>
#include <lib.h>
#include <memory.h>
#include <mmap.h>

/*
  system call handler for clone, create thread like
  execution contexts. Returns pid of the new context to the caller.
  The new context starts execution from the 'th_func' and
  use 'user_stack' for its stack
*/
long do_clone(void *th_func, void *user_stack, void *user_arg) {
  struct exec_context *new_ctx = get_new_ctx();
  struct exec_context *ctx = get_current_ctx();

  u32 pid = new_ctx->pid;

  if (!ctx->ctx_threads) {  // This is the first thread
    ctx->ctx_threads = os_alloc(sizeof(struct ctx_thread_info));
    bzero((char *)ctx->ctx_threads, sizeof(struct ctx_thread_info));
    ctx->ctx_threads->pid = ctx->pid;
  }

  /* XXX Do not change anything above. Your implementation goes here*/

  struct thread *curr_thread = find_unused_thread(ctx);

  // max no of threads allocated
  if (curr_thread == NULL) {
    return -1;
  }
  // allocate page for os stack in kernel part of process's VAS
  // The following two lines should be there. The order can be
  // decided depending on your logic.
  setup_child_context(new_ctx);
  new_ctx->type = EXEC_CTX_USER_TH;  // Make sure the context type is thread
  new_ctx->ppid = ctx->pid;

  new_ctx->used_mem = ctx->used_mem;  // not required
  new_ctx->pgd = ctx->pgd;
  new_ctx->regs = ctx->regs;

  for (int i = 0; i < CNAME_MAX; i++)
    new_ctx->name[i] = ctx->name[i];  // not required

  new_ctx->pending_signal_bitmap = ctx->pending_signal_bitmap;  // not required

  for (int i = 0; i < MAX_SIGNALS; i++)
    new_ctx->sighandlers[i] = ctx->sighandlers[i];  // not required

  new_ctx->ticks_to_sleep = ctx->ticks_to_sleep;        // not required
  new_ctx->ticks_to_alarm = ctx->ticks_to_alarm;        // not required
  new_ctx->alarm_config_time = ctx->alarm_config_time;  // not required

  for (int i = 0; i < MAX_OPEN_FILES; i++)
    new_ctx->files[i] = ctx->files[i];  // required

  new_ctx->ctx_threads = NULL;  // this ensures that cleanup_all_threads() is
                                // not called when a thread exits, The cleanup
                                // only happens after parent context exits

  new_ctx->vm_area = ctx->vm_area;  // this will change

  for (int i = 0; i < MAX_MM_SEGS; i++) {
    new_ctx->mms[i] = ctx->mms[i];
  }

  // function call
  new_ctx->regs.entry_rsp = (u64)user_stack - 8;  // 8 bytes for gthread_return
  new_ctx->regs.rbp = (u64)user_stack;            // !why?
  new_ctx->regs.entry_rip = (u64)th_func;
  new_ctx->regs.rdi = (u64)user_arg;

  // explicit call of make_thread_ready is required
  new_ctx->state = WAITING;

  // thread information in parent
  curr_thread->parent_ctx = ctx;
  curr_thread->pid = new_ctx->pid;
  curr_thread->status = TH_WAITING;

  return new_ctx->pid;
}

static u64 get_val(u64 addr) { return *(u64 *)addr; }
static void set_val(u64 addr, u64 val) { *(u64 *)addr = val; }

static int get_flag(int perm) {
  switch (perm) {
    case PROT_READ | PROT_WRITE | TP_SIBLINGS_RDWR:
      return 0x7;
    case PROT_READ | PROT_WRITE | TP_SIBLINGS_RDONLY:
      return 0x5;  // write access is not allowed
    case PROT_READ | PROT_WRITE | TP_SIBLINGS_NOACCESS:
      return 0x1;  // user access is not allowed
    default:
      printk("get_flag:: strage permissions\n");
  }
  return 0;
}

static int valid_access(int flag, int error_code) {
  // ! CheckPoint : What other types of error are possible

  if (!(flag >> 2)) return 0;  // siblings not allowed

  // write is requested no write permissions
  if ((error_code & PF_ERROR_WR) & (!(flag & PF_ERROR_WR))) return 0;

  return 1;  // read is always allowed
}

static struct thread_private_map *find_th_pmap(struct exec_context *current,
                                               u64 addr) {
  struct thread *th;
  struct thread_private_map *thmap;

  // context must be of parent
  if (!isProcess(current)) {
    printk("find_th_pmap:: parent's context must be passed\n");
    return NULL;
  }

  th = &current->ctx_threads->threads[0];
  for (int i = 0; i < MAX_THREADS; i++, th++) {
    thmap = &th->private_mappings[0];
    for (int j = 0; j < MAP_TH_PRIVATE; j++, thmap++) {
      if (!thmap->owner) continue;

      // range check logic
      if (addr >= thmap->start_addr && addr < thmap->start_addr + thmap->length)
        return thmap;
    }
  }
  return NULL;
}

#define P_OFFSET 8  // 8 bytes == 64 bit == page table entry size

static int fix_page_table(u64 pgd, u64 VA, int flag, int update) {
  u64 pgd_entry, pud, pud_entry, pmd, pmd_entry, pte, pte_entry;
  u64 temp_pfn;

  pgd = (u64)osmap(pgd);  // page number to virtual address

  // if only update of permissions is requested don't allocate PFNs
  // When needed a page fault will be raised and the page will be
  // allocated

  //=================================================

  pgd_entry = pgd + ((VA & PGD_MASK) >> PGD_SHIFT) * P_OFFSET;

  // permissions at intermediate level are not checked, assuming we
  // have both read and write permissions in all of them. Only the
  // permissions at leaf level ( capacity 2^21 > GMALLOC ) are updated

  if (!(get_val(pgd_entry) & 1)) {
    if (update) return 1;
    temp_pfn = (u64)os_pfn_alloc(OS_PT_REG);
    set_val(pgd_entry, (temp_pfn << PAGE_SHIFT) | 0x7);
  }

  //=================================================

  pud = (u64)osmap(get_val(pgd_entry) >> PAGE_SHIFT);
  pud_entry = pud + ((VA & PUD_MASK) >> PUD_SHIFT) * P_OFFSET;

  if (!(get_val(pud_entry) & 1)) {
    if (update) return 1;
    temp_pfn = (u64)os_pfn_alloc(OS_PT_REG);
    set_val(pud_entry, (temp_pfn << PAGE_SHIFT) | 0x7);
  }
  //=================================================

  pmd = (u64)osmap(get_val(pud_entry) >> PAGE_SHIFT);
  pmd_entry = pmd + ((VA & PMD_MASK) >> PMD_SHIFT) * P_OFFSET;

  if (!(get_val(pmd_entry) & 1)) {
    if (update) return 1;
    temp_pfn = (u64)os_pfn_alloc(OS_PT_REG);
    set_val(pmd_entry, (temp_pfn << PAGE_SHIFT) | 0x7);
  }

  //=================================================

  pte = (u64)osmap(get_val(pmd_entry) >> PAGE_SHIFT);
  pte_entry = pte + ((VA & PTE_MASK) >> PTE_SHIFT) * P_OFFSET;

  if (!(get_val(pte_entry) & 1)) {
    if (update) return 1;
    temp_pfn = (u64)os_pfn_alloc(USER_REG);
    set_val(pte_entry, temp_pfn << PAGE_SHIFT);
  }

  //=================================================

  // this time the flag must be exactly as required
  set_val(pte_entry, (get_val(pte_entry) & FLAG_MASK) | flag);
  // invalidate entry corresponding to this VA in TLB
  // Note that TLB is managed by Hardware and need to be flushed
  asm volatile("invlpg (%0)" ::"r"(VA) : "memory");

  //=================================================

  return 1;
}

/*This is the page fault handler for thread private memory area (allocated
 * using gmalloc from user space). This should fix the fault as per the rules.
 * If the the access is legal, the fault handler should fix it and return 1.
 * Otherwise it should invoke segfault_exit and return -1*/

int handle_thread_private_fault(struct exec_context *current, u64 addr,
                                int error_code) {
  struct exec_context *parent;
  struct thread *th;
  struct thread_private_map *thmap;
  int flag = 0x7;

  // Owner logic
  if (isProcess(current)) {
    thmap = find_th_pmap(current, addr);

    if (!thmap) {
      segfault_exit(current->pid, current->regs.entry_rip, addr);
      return -1;
    }
    // parent has both read and write access to all private mappings
    return fix_page_table(current->pgd, addr, flag, error_code & 1);
  }

  parent = get_ctx_by_pid(current->ppid);
  th = find_thread_from_pid(parent, current->pid);
  thmap = find_th_pmap(parent, addr);

  if (!thmap) {
    segfault_exit(current->pid, current->regs.entry_rip, addr);
    return -1;
  }

  if (thmap->owner != th) {
    flag = get_flag(thmap->flags);
  }

  if (!valid_access(flag, error_code)) {
    segfault_exit(current->pid, current->regs.entry_rip, addr);
    return -1;
  }

  // must be fixed now
  return fix_page_table(current->pgd, addr, flag, error_code & 1);
}

/*This is a handler called from scheduler. The 'current' refers to the
 * outgoing context and the 'next' is the incoming context. Both of them can
 * be either the parent process or one of the threads, but only one of them
 * can be the process (as we are having a system with a single user process).
 * This handler should apply the mapping rules passed in the gmalloc calls. */

int handle_private_ctxswitch(struct exec_context *current,
                             struct exec_context *next) {
  /* your implementation goes here*/

  struct thread *th;
  struct thread_private_map *thmap;
  struct exec_context *parent;
  struct thread *next_thread = NULL;
  int flag;  // change only if siblings are not allowed

  if (isThread(next)) {
    parent = get_ctx_by_pid(next->ppid);
    next_thread = find_thread_from_pid(parent, next->pid);
  } else {
    parent = next;
  }

  // for other threads the flag depends on protection
  th = &parent->ctx_threads->threads[0];
  // atmax 4 iterations
  for (int i = 0; i < MAX_THREADS; i++, th++) {
    thmap = &th->private_mappings[0];

    // atmax 4 iteration
    for (int i = 0; i < MAP_TH_PRIVATE; i++, thmap++) {
      if (!thmap->owner) continue;

      if ((th != next_thread) && (next_thread != NULL))
        flag = get_flag(thmap->flags);  // neither parent nor owner
      else
        flag = 0x7;  // otherwise read and write permissons

      // page size is 4KB, fix one page at a time
      // Atmax 2^8 = 256 iteration for GALLOC_MAX = 2^20
      for (u64 j = 0; j < thmap->length; j += PAGE_SIZE) {
        fix_page_table(next->pgd, thmap->start_addr + j, flag, 1);
      }
    }
  }

  return 0;
}
