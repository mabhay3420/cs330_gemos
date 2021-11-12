#include <gthread.h>
#include <ulib.h>

static struct process_thread_info tinfo
    __attribute__((section(".user_data"))) = {};
/*XXX
      Do not modifiy anything above this line. The global variable tinfo
   maintains user level accounting of threads. Refer gthread.h for definition of
   related structs.
 */

/* Returns 0 on success and -1 on failure */
/* Here you can use helper system call "make_thread_ready" for your
 * implementation */

static void gthread_return() {
  void *ret_val;
  asm volatile("mov %%rax, %0;" : "=r"(ret_val) : : "memory", "rax");
  // printf("gthread_return:: ret_val : %x\n", ret_val);
  gthread_exit(ret_val);
}

int gthread_create(int *tid, void *(*fc)(void *), void *arg) {

  /* You need to fill in your implementation here*/
  if (tinfo.num_threads == MAX_THREADS) {
    printf("gthread_create:: No more threads can be created\n");
    return -1;
  }

  // allocate a stack for thread
  void *stackp = mmap(NULL, TH_STACK_SIZE, PROT_READ | PROT_WRITE, 0);
  if (!stackp || stackp == MAP_ERR) {
    printf("gthread_create:: Stack Allocation Failed\n");
    return -1;
  }
  *(u64 *)((u64)stackp + TH_STACK_SIZE - 8) = (u64)&gthread_return;

  long thpid = clone(fc, (u64)stackp + TH_STACK_SIZE, arg);
  if (thpid <= 0) {
    printf("gthread_create:: Thread Cloning Failed\n");
    return -1;
  }

  // update userspace information
  struct thread_info *curr_thread;
  for (int i = 0; i < MAX_THREADS; i++) {
    curr_thread = &tinfo.threads[i];
    if (curr_thread->status == TH_STATUS_UNUSED) {
      curr_thread->tid = i;
      break;
    }
  }

  *tid = curr_thread->tid;
  curr_thread->pid = thpid;
  curr_thread->stack_addr = stackp;
  curr_thread->status = TH_STATUS_ALIVE;
  tinfo.num_threads++;

  make_thread_ready(thpid);
  return 0;
}

int gthread_exit(void *retval) {

  /* You need to fill in your implementation here*/
  int thpid = getpid();
  struct thread_info *curr_thread;
  for (int i = 0; i < MAX_THREADS; i++) {
    curr_thread = &tinfo.threads[i];
    if (curr_thread->status == TH_STATUS_UNUSED)
      continue;
    if (curr_thread->pid == thpid)
      break;
  }
  int err = munmap((void *)((u64)curr_thread->stack_addr - TH_STACK_SIZE),
                   TH_STACK_SIZE);
  if (err < 0) {
    printf("gthread_exit :: error while unmapping\n");
    return -1;
  }
  // printf("gthread_exit :: return address : %x\n", retval);
  curr_thread->ret_addr = retval;
  curr_thread->status = TH_STATUS_USED;
  // call exit
  exit(0);
}

void *gthread_join(int tid) {

  /* Here you can use helper system call "wait_for_thread" for your
   * implementation */
  /* You need to fill in your implementation here*/

  printf("joining threads\n");
  int ctr;
  struct thread_info *curr_thread = &tinfo.threads[0];
  for (ctr = 0; ctr < MAX_THREADS; ctr++, curr_thread++) {
    if (curr_thread->tid == tid)
      break;
  }

  if (ctr == MAX_THREADS) {
    printf("gthread_join:: No such thread exists\n");
    return NULL;
  }

  switch (curr_thread->status) {
  case TH_STATUS_UNUSED:
    return NULL; // join called on dead function

  case TH_STATUS_USED:
    break;

  case TH_STATUS_ALIVE:
    if (wait_for_thread(curr_thread->pid) < 0) {
      printf("gthread_join:: Thread died or invalid thread pid\n");
      return NULL;
    }
    break;

  default:
    return NULL;
  }

  // cleaning and updation
  curr_thread->status = TH_STATUS_UNUSED;
  tinfo.num_threads--;
  return curr_thread->ret_addr;
}

/*Only threads will invoke this. No need to check if its a process
 * The allocation size is always < GALLOC_MAX and flags can be one
 * of the alloc flags (GALLOC_*) defined in gthread.h. Need to
 * invoke mmap using the proper protection flags (for prot param to mmap)
 * and MAP_TH_PRIVATE as the flag param of mmap. The mmap call will be
 * handled by handle_thread_private_map in the OS.
 * */

void *gmalloc(u32 size, u8 alloc_flag) {

  /* You need to fill in your implementation here*/
  int flag = PROT_READ | PROT_WRITE;
  switch (alloc_flag) {
  case GALLOC_OWNONLY:
    flag |= TP_SIBLINGS_NOACCESS;
    break;
  case GALLOC_OTRDONLY:
    flag |= TP_SIBLINGS_RDONLY;
    break;
  case GALLOC_OTRDWR:
    flag |= TP_SIBLINGS_RDWR;
    break;
  default:
    return NULL;
  }

  // get current thread
  int thpid = getpid();
  if (thpid < 0) {
    printf("gmalloc:: invalid thread pid\n");
    return NULL;
  }

  int ctr;
  struct thread_info *curr_thread = &tinfo.threads[0];
  for (ctr = 0; ctr < MAX_THREADS; ctr++, curr_thread++) {
    if (curr_thread->pid == thpid)
      break;
  }

  if (ctr == MAX_THREADS) {
    printf("gmalloc:: No thread information for current thread\n");
    return NULL;
  }

  struct galloc_area *curr_mmap = &curr_thread->priv_areas[0];
  for (ctr = 0; ctr < MAP_TH_PRIVATE; ctr++, curr_mmap++) {
    if (!curr_mmap->owner)
      break;
  }

  if (ctr == MAP_TH_PRIVATE) {
    printf("gmalloc:: No more mappings allowed\n");
    return NULL;
  }

  void *addr = mmap(NULL, size, flag, MAP_TH_PRIVATE);
  if (addr == NULL) {
    printf("gmalloc:: mmap failed\n");
    return NULL;
  }
  printf("galloc:: addr : %x \n", addr);

  curr_mmap->start = (u64)addr;
  curr_mmap->length = size;
  curr_mmap->flags = flag;
  curr_mmap->owner = curr_thread;

  return addr;
}
/*
   Only threads will invoke this. No need to check if the caller is a process.
*/
int gfree(void *ptr) {

  /* You need to fill in your implementation here*/

  int thpid = getpid();
  if (thpid < 0) {
    printf("gfree:: Invalid pid\n");
    return -1;
  }

  int ctr;
  struct thread_info *curr_thread = &tinfo.threads[0];
  for (ctr = 0; ctr < MAX_THREADS; ctr++, curr_thread++) {
    if (curr_thread->pid == thpid)
      break;
  }

  if (ctr == MAX_THREADS) {
    printf("gfree:: No thread information for current thread\n");
    return -1;
  }

  struct galloc_area *curr_mmap = &curr_thread->priv_areas[0];
  for (ctr = 0; ctr < MAP_TH_PRIVATE; ctr++, curr_mmap++) {
    if (!curr_mmap->owner && curr_mmap->start == (u64)ptr)
      break;
  }

  if (ctr == MAP_TH_PRIVATE) {
    printf("gfree:: Invalid access\n");
    return -1;
  }

  // update the userspace information
  if (munmap(ptr, curr_mmap->length) < 0) {
    printf("gfree:: munmap failed\n");
    return -1;
  }

  curr_mmap->owner = NULL;

  return 0;
}
