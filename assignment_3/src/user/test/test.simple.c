#include <ulib.h>

void do_end_handler() { printf("In the end Handler\n"); }

int fn_1() {
  printf("In Fn 1\n");
  return 0;
}

int main(u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5) {
  printf("Check where the rip resides in kernel stack\n");
  long ret = 0;
  int cpid;
  ret = become_debugger(do_end_handler);
  cpid = fork();
  if (cpid < 0) {
    printf("Error in Fork\n");
  } else if (cpid == 0) {
    printf("Child calls f1\n");
    fn_1();
    printf("fn1 execution complete\n");
  } else {
    u64 child_instr;
    struct registers regs;
    ret = set_breakpoint(fn_1, 0);
    printf("Debugger sets breakpoint at fn1\n");
    ret = wait_and_continue();
    printf("Child stopped at fn_1()");
    info_registers(&regs);
    printf("Child rsp stored: %x\n", regs.entry_rsp);
    printf("Current rip: %x\n", regs.entry_rip);
    ret = wait_and_continue();
    printf("in parent main Child exited\n");
  }

  //   printf("Hello world\n");
  return 0;
}
