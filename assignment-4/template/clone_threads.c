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
  if (curr_thread == NULL) {
    printk("do_clone :: MAX_THREADS already exist, no new can be created\n");
    return -1;
  }
  // allocate page for os stack in kernel part of process's VAS
  // The following two lines should be there. The order can be
  // decided depending on your logic.
  setup_child_context(new_ctx);
  new_ctx->type = EXEC_CTX_USER_TH;  // Make sure the context type is thread
  new_ctx->ppid = ctx->pid;

  // Copy everything blindly
  new_ctx->used_mem = ctx->used_mem;
  new_ctx->pgd = ctx->pgd;
  new_ctx->regs = ctx->regs;
  for (int i = 0; i < CNAME_MAX; i++) new_ctx->name[i] = ctx->name[i];
  new_ctx->pending_signal_bitmap = ctx->pending_signal_bitmap;
  for (int i = 0; i < MAX_SIGNALS; i++)
    new_ctx->sighandlers[i] = ctx->sighandlers[i];
  new_ctx->ticks_to_sleep = ctx->ticks_to_sleep;
  new_ctx->ticks_to_alarm = ctx->ticks_to_alarm;
  new_ctx->alarm_config_time = ctx->alarm_config_time;
  for (int i = 0; i < MAX_OPEN_FILES; i++) new_ctx->files[i] = ctx->files[i];
  new_ctx->ctx_threads = ctx->ctx_threads;  // not required
  new_ctx->vm_area = ctx->vm_area;
  for (int i = 0; i < MAX_MM_SEGS; i++) {
    new_ctx->mms[i] = ctx->mms[i];
  }

  // function call
  new_ctx->regs.entry_rsp = (u64)user_stack - 8;  // 8 bytes for gthread_return
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

static struct thread_private_map *find_th_pmap(struct exec_context *current,
                                               u64 addr) {
  struct thread *th;
  struct thread_private_map *thmap;

  // parent
  if (!isProcess(current)) return NULL;

  th = &current->ctx_threads->threads[0];
  for (int i = 0; i < MAX_THREADS; i++, th++) {
    thmap = &th->private_mappings[0];
    for (int j = 0; j < MAP_TH_PRIVATE; j++, thmap++) {
      // whether this mapping is valid or not
      if (!thmap->owner) continue;

      // range check logic
      if (addr >= thmap->start_addr && addr < thmap->start_addr + thmap->length)
        return thmap;
    }
  }
  return NULL;
}

static u64 get_val(u64 addr) { return *(u64 *)addr; }
static void set_val(u64 addr, u64 val) { *(u64 *)addr = val; }

static int fix_page_table(u64 pgd, u64 VA, int flag) {
  u64 pgd_entry, pud, pud_entry, pmd, pmd_entry, pte, pte_entry;
  u64 temp_pfn;

  pgd_entry = pgd + (VA >> 39);
  // pgd_entry not present
  if (!(get_val(pgd_entry) & 0x1)) {
    temp_pfn = os_pfn_alloc(OS_PT_REG);
    set_val(pgd_entry, (temp_pfn << 12) | 0x7);
  }

  pud = osmap(pgd_entry >> 12);
  pud_entry = pud + (VA >> 30) & 0x1FF;

  if (!(get_val(pud_entry) & 0x1)) {
    temp_pfn = os_pfn_alloc(OS_PT_REG);
    set_val(pud_entry, (temp_pfn << 12) | 0x7);
  }

  pmd = osmap(pud_entry >> 12);
  pmd_entry = pmd + (VA >> 21) & 0x1FF;

  if (!(get_val(pmd_entry) & 0x1)) {
    temp_pfn = os_pfn_alloc(OS_PT_REG);
    set_val(pmd_entry, (temp_pfn << 12) | 0x7);
  }

  pte = osmap(pmd_entry >> 12);
  pte_entry = pte + (VA >> 12) & 0x1FF;

  if (!(get_val(pte_entry) & 0x1)) {
    temp_pfn = os_pfn_alloc(USER_REG);
    set_val(pte_entry, temp_pfn << 12);
  }

  // this time the flag must be exactly as required
  set_val(pte_entry, ((get_val(pte_entry) >> 3) << 3) | flag);
  return 1;
}

/*This is the page fault handler for thread private memory area (allocated using
 * gmalloc from user space). This should fix the fault as per the rules. If the
 * the access is legal, the fault handler should fix it and return 1. Otherwise
 * it should invoke segfault_exit and return -1*/

int handle_thread_private_fault(struct exec_context *current, u64 addr,
                                int error_code) {
  /* your implementation goes here*/
  // find the owner
  struct exec_context *parent;
  struct thread *th;
  struct thread_private_map *thmap;

  // Owner logic
  if (isProcess(current)) {
    parent = current;
    thmap = th_pmap(current, addr);
    if (!thmap) {
      printk("th_pvt_flt:: address is not mapped\n");
      return -1;
    }
    th = thmap->owner;
  } else {
    parent = get_ctx_by_pid(current->ppid);
    th = find_thread_from_pid(parent, current->pid);
    thmap = find_th_pmap(parent, addr);
    if (!thmap) {
      printk("th_pvt_flt:: address is not mapped\n");
      return -1;
    }

    if (thmap->owner != th &&
        thmap->flags == PROT_READ | PROT_WRITE | TP_SIBLINGS_NOACCESS) {
      printk("th_pvt_flt:: sibling is not allowed at all\n");
      return -1;
    }
  }

  // fault handling : If accesses are valid then space must be
  // allocated with proper flags in page table

  // for parent both read and write bit must be set
  // for siblings it depends on the flag
  // for owner thread both read and write bit must be set
  int flag = 0x7;
  if (thmap->owner != th &&
      thmap->flags == PROT_READ | PROT_WRITE | TP_SIBLINGS_RDONLY)
    flag = 0x5;
  switch (error_code) {
    case 0x4:  // user mode read access to unmapped page
      return fix_page_table(current->pgd, addr, flag);
    case 0x6:  // user mode write access to unmapped page
      if (flag == 0x7) return fix_page_table(current->pgd, addr, flag);
      break;
    case 0x7:  // user mode write access to read-only page
      if (flag == 0x7) return fix_page_table(current->pgd, addr, flag);
      break;
    default:
      printf("th_pvt_flt :: strange error code \n");
  }
  segfault_exit(current->pid, current->regs.entry_rip, addr);
  return -1;
}

/*This is a handler called from scheduler. The 'current' refers to the
 * outgoing context and the 'next' is the incoming context. Both of them can
 * be either the parent process or one of the threads, but only one of them
 * can be the process (as we are having a system with a single user process).
 * This handler should apply the mapping rules passed in the gmalloc calls. */

int handle_private_ctxswitch(struct exec_context *current,
                             struct exec_context *next) {
  /* your implementation goes here*/
  // If the next process is parent then let us raise a page fault
  if (isProcess(next)) return 0;

  // if next process is thread, then we don't want it to access the
  // mapping which are not its own and are either siblings read only
  // or sibling no access

  // if the current process is thread then only mappings related to
  // current has to be checked
  struct thread *th;
  struct thread_private_map *thmap;
  struct exec_context *parent = get_ctx_by_pid(current->ppid);
  int flag = 0x7;  // change only if siblings are not allowed
  if (isThread(current)) {
    th = find_thread_from_pid(parent, current->pid);
    thmap = &th->private_mappings[0];
    for (int i = 0; i < MAP_TH_PRIVATE; i++, thmap++) {
      if (!thmap->owner) continue;
      // page size is 4KB, fix one page at a time
      for (int j = 0; j < thmap->length; j += 4096) {
        // fix_page_table(current->pgd, thmap->start_addr + j, )
      }
    }
  }

  // if previously we had parent then sanity check all mappings

  // No need to check mappings related to next at all. raise page
  // fault if not enough permissions
  return 0;
}
