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
  gthread_exit(ret_val);
}

int gthread_create(int *tid, void *(*fc)(void *), void *arg) {
  if (tinfo.num_threads == MAX_THREADS) {
    // printf("gthread_create:: No more threads can be created\n");
    return -1;
  }

  // allocate a stack for thread
  void *stackp = mmap(NULL, TH_STACK_SIZE, PROT_READ | PROT_WRITE, 0);
  if (!stackp || stackp == MAP_ERR) {
    // printf("gthread_create:: Stack Allocation Failed\n");
    return -1;
  }
  *(u64 *)(((u64)stackp) + TH_STACK_SIZE - 8) = (u64)(&gthread_return);

  long thpid = clone(fc, (u64)stackp + TH_STACK_SIZE, arg);
  if (thpid <= 0) {
    // printf("gthread_create:: Thread Cloning Failed\n");
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

  *tid = (int)curr_thread->tid;
  curr_thread->pid = thpid;
  curr_thread->stack_addr = stackp + TH_STACK_SIZE;
  curr_thread->status = TH_STATUS_ALIVE;
  // in case return address is not filled simply return
  // NULL, helpful in case of segfault
  curr_thread->ret_addr = NULL;

  tinfo.num_threads++;

  make_thread_ready(thpid);
  return 0;
}

int gthread_exit(void *retval) {
  int thpid = getpid();
  int ctr;
  struct thread_info *curr_thread;
  for (ctr = 0; ctr < MAX_THREADS; ctr++) {
    curr_thread = &tinfo.threads[ctr];
    // no thing as waiting
    if (curr_thread->status == TH_STATUS_UNUSED ||
        curr_thread->status == TH_STATUS_USED)
      continue;
    if (curr_thread->pid == thpid) break;
  }

  if (ctr == MAX_THREADS) {
    // printf(
    // "gthread_exit:: information of thread [%d] calling exit is not "
    // "updated ! Probably status not updated properly\n",
    // thpid);
    return -1;
  }

  curr_thread->ret_addr = retval;  // can be null
  curr_thread->status = TH_STATUS_USED;

  // only tid and ret_addr are maintained
  exit(0);
}

void *gthread_join(int tid) {
  int ctr;
  struct thread_info *curr_thread = &tinfo.threads[0];
  for (ctr = 0; ctr < MAX_THREADS; ctr++, curr_thread++) {
    if (curr_thread->status == TH_STATUS_UNUSED) continue;
    if (curr_thread->tid == tid) break;
  }

  if (ctr == MAX_THREADS) {
    return NULL;  // join called on dead thread
  }

  switch (curr_thread->status) {
    case TH_STATUS_USED:
      break;  // value must be available

    case TH_STATUS_ALIVE:
      // return of value 0 from wait call just signifies that one
      // round of schedulling was succesful.
      while (1) {
        // !!! PIDs ARE REUSABLE so don't depend on return value of wait

        // Clearly thread has some valid pid in the starting, so returning error
        // means the thread has exited and either the pid is not assigned or
        // assigned to some other thread. No other case is possible
        int sig = wait_for_thread(curr_thread->pid);

        // 1. Thread returned normally
        // curr_thread->ret_addr =  NON-NULL(set by gthread_exit())
        if (curr_thread->ret_addr) {
          break;
        }

        // sig < 0 : invalid pid : 2 cases
        // 1. segfault() -> process exits -> pid available but not reassigned ->
        //    wait call -> (invalid pid()) curr_thread->ret_val = NULL (default)
        //    as gthread_exit() not called

        // 2. normal exit -> pid available but not reassigned ( invalid pid() )
        //    -> wait call -> (curr_thread-> ret_val) must have been set by
        //    gthread_exit()
        if (sig < 0) {
          break;
        }

        // sig = 0 and curr_thread->ret_val = NULL -> pid is valid : 2 cases
        // 1. Original thread exited -> pid available to be reassigned -> pid
        //    reassigned to some "other thread" and the "other thread" is in
        //    execution -> curr_thread->ret_addr = NULL ( NON-NULL is already
        //    handler above )
        // 2. Original Thread still in execution -> (curr_thread -> ret_addr =
        //    NULL(the default one))

        // valid pid but must check whether it is reassigned or not?
        struct thread_info *other_thread = &tinfo.threads[0];
        for (ctr = 0; ctr < MAX_THREADS; ctr++, other_thread++) {
          if (other_thread->tid == curr_thread->tid)
            continue;  // its the same thread
          if (other_thread->pid == curr_thread->pid) break;  // reassigned
        }

        // something fishy, we waited for wrong thread, the original
        // already exited with return value NULL
        if (ctr != MAX_THREADS) {
          break;
        }

        // thread not finished yet, wait again
      }
      break;
  }

  // stack_addr is the bottom of stack(Higher address), for unmap
  // we need to pass the start of segment(lower address)
  int err = munmap((void *)((u64)curr_thread->stack_addr - TH_STACK_SIZE),
                   TH_STACK_SIZE);
  if (err < 0) {
    // printf("gthread_join :: error while unmapping the stack\n");
    return NULL;
  }

  // cleaning and updation
  tinfo.num_threads--;
  curr_thread->status = TH_STATUS_UNUSED;

  // not required but just to ensure in case we used them somewhere
  // error should be thrown
  curr_thread->stack_addr = NULL;
  curr_thread->pid = -1;
  curr_thread->tid = -1;
  // exit system call will remove all mappings
  // the same should be reflected here
  for (int i = 0; i < MAP_TH_PRIVATE; i++) {
    curr_thread->priv_areas[i].owner = NULL;
  }

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
    // printf("gmalloc:: invalid thread pid\n");
    return NULL;
  }

  int ctr;
  struct thread_info *curr_thread = &tinfo.threads[0];
  for (ctr = 0; ctr < MAX_THREADS; ctr++, curr_thread++) {
    // a pid can be assigned to multiple threads,
    // though atmost one will be Alive
    if (curr_thread->status != TH_STATUS_ALIVE) continue;

    if (curr_thread->pid == thpid) break;
  }

  if (ctr == MAX_THREADS) {
    // printf("gmalloc:: Not a registered thread\n");
    return NULL;
  }

  struct galloc_area *curr_mmap = &curr_thread->priv_areas[0];
  for (ctr = 0; ctr < MAP_TH_PRIVATE; ctr++, curr_mmap++) {
    if (!curr_mmap->owner) break;
  }

  if (ctr == MAP_TH_PRIVATE) {
    // printf("gmalloc:: No more mappings allowed\n");
    return NULL;
  }

  void *addr = mmap(NULL, size, flag, MAP_TH_PRIVATE);
  if (!addr || addr == MAP_ERR) {
    // printf("gmalloc:: mmap failed\n");
    return NULL;
  }

  // update information
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
  int thpid = getpid();
  if (thpid < 0) {
    // printf("gfree:: Invalid pid\n");
    return -1;
  }

  int ctr;
  struct thread_info *curr_thread = &tinfo.threads[0];
  for (ctr = 0; ctr < MAX_THREADS; ctr++, curr_thread++) {
    // it might happen that two threads are assigned same pid
    // because the join functionality is only a userspace
    // construct. For OS the pid of exiting thread is freely
    // available and will be reused, so we check for status here.
    if (curr_thread->status == TH_STATUS_ALIVE && curr_thread->pid == thpid)
      break;
  }

  if (ctr == MAX_THREADS) {
    // printf(
    // "gfree:: BUG!!! Thread information of [%d] in userspace not update "
    // "properly\n",
    // thpid);
    return -1;
  }

  struct galloc_area *curr_mmap = &curr_thread->priv_areas[0];
  for (ctr = 0; ctr < MAP_TH_PRIVATE; ctr++, curr_mmap++) {
    if (!curr_mmap->owner) continue;  // not allocated
    if (curr_mmap->start == (u64)ptr) break;
  }

  if (ctr == MAP_TH_PRIVATE) {
    // printf("gfree:: Invalid address or thread is not the owner of
    // address\n");
    return -1;
  }

  // update the userspace information
  if (munmap(ptr, curr_mmap->length) < 0) {
    // printf("gfree:: munmap failed\n");
    return -1;
  }

  curr_mmap->owner = NULL;
  return 0;
}
