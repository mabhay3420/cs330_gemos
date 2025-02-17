#include <gthread.h>
#include <ulib.h>

/*XXX   Do not declare global or static variables in the test cases*/

/*Thread functions must be declared as static*/
static void *thfunc1(void *arg) {
  int ctr;
  u64 *th_priv_ptr = (u64 *)arg;
  // Allocate and place the pointer
  // in thread argument i.e., &th_priv_addr
  char *ptr = (char *)gmalloc(8192, GALLOC_OWNONLY);

  // printf("thfunc1:: ptr : %x\n", ptr);

  for (ctr = 0; ctr < 100; ++ctr) {
    ptr[ctr] = 'a' + (ctr % 26);
  }
  ptr[ctr] = 0;
  *th_priv_ptr = (u64)ptr;
  // printf("thfunc1:: ptr = %x\n", *th_priv_ptr);
  sleep(10);  // Need to sleep for other thread to finish
  // printf("thfunc1:: done sleeping\n");
  gfree((void *)ptr);
  // printf("thfunc1:: done freeing\n");
  return NULL;
}

static void *thfunc2(void *arg) {
  char *ptr;
  u64 *th_priv_ptr = (u64 *)arg;
  while (*th_priv_ptr == 0) sleep(1);
  // printf("[3] tried to access private pointer, should raise an error\n");
  // printf("thfunc2:: ptr = %x\n", *th_priv_ptr);
  ptr = (char *)*th_priv_ptr;
  *ptr = 'c';
  printf("%s\n", ptr);  // Is not allowed
  return NULL;
}

int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
  u64 th_priv_addr = 0;
  int tid1, tid2;
  if (gthread_create(&tid1, thfunc1, (void *)&th_priv_addr) < 0) {
    printf("gthread_create failed\n");
    exit(-1);
  }
  printf("Created thread: %d\n", tid1);

  if (gthread_create(&tid2, thfunc2, (void *)&th_priv_addr) < 0) {
    printf("gthread_create failed\n");
    exit(-1);
  }
  printf("Created thread: %d\n", tid2);
  gthread_join(tid1);
  printf("Thread %d returned\n", tid1);
  gthread_join(tid2);
  printf("Thread %d returned\n", tid2);
  return 0;
}
