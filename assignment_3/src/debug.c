#include <context.h>
#include <debug.h>
#include <entry.h>
#include <lib.h>
#include <memory.h>

/*****************************HELPERS******************************************/

/*
 * allocate the struct which contains information about debugger
 *
 */
struct debug_info *alloc_debug_info() {
  struct debug_info *info =
      (struct debug_info *)os_alloc(sizeof(struct debug_info));
  if (info) bzero((char *)info, sizeof(struct debug_info));
  return info;
}
/*
 * frees a debug_info struct
 */
void free_debug_info(struct debug_info *ptr) {
  if (ptr) os_free((void *)ptr, sizeof(struct debug_info));
}

/*
 * allocates a page to store registers structure
 */
struct registers *alloc_regs() {
  struct registers *info =
      (struct registers *)os_alloc(sizeof(struct registers));
  if (info) bzero((char *)info, sizeof(struct registers));
  return info;
}

/*
 * frees an allocated registers struct
 */
void free_regs(struct registers *ptr) {
  if (ptr) os_free((void *)ptr, sizeof(struct registers));
}

/*
 * allocate a node for breakpoint list
 * which contains information about breakpoint
 */
struct breakpoint_info *alloc_breakpoint_info() {
  struct breakpoint_info *info =
      (struct breakpoint_info *)os_alloc(sizeof(struct breakpoint_info));
  if (info) bzero((char *)info, sizeof(struct breakpoint_info));
  return info;
}

/*
 * frees a node of list
 */
void free_breakpoint_info(struct breakpoint_info *ptr) {
  if (ptr) os_free((void *)ptr, sizeof(struct breakpoint_info));
}

/*
 * free while breakpoint list
 */
void free_breakpoint_info_list(struct breakpoint_info *ptr) {
  if (ptr && ptr->next) free_breakpoint_info_list(ptr->next);
  if (ptr) free_breakpoint_info(ptr);
}
/*
 * allocate a node for call_stack list
 * which contains information about call_stack
 */
struct stack_func_info *alloc_stack_func_info() {
  struct stack_func_info *info =
      (struct stack_func_info *)os_alloc(sizeof(struct stack_func_info));
  if (info) bzero((char *)info, sizeof(struct stack_func_info));
  return info;
}

/*
 * frees a node of call_stack list
 */
void free_stack_func_info(struct stack_func_info *ptr) {
  if (ptr) os_free((void *)ptr, sizeof(struct stack_func_info *));
}

/*
 * Free complete call_stack_list
 */
void free_call_stack_list(struct stack_func_info *ptr) {
  if (ptr && ptr->caller) free_call_stack_list(ptr->caller);
  free_stack_func_info(ptr);
}

/*
 * Fork handler.
 * The child context doesnt need the debug info
 * Set it to NULL
 * The child must go to sleep( ie move to WAIT state)
 * It will be made ready when the debugger calls wait
 */
void debugger_on_fork(struct exec_context *child_ctx) {
  // printk("DEBUGGER FORK HANDLER CALLED\n");
  struct exec_context *parent_context;
  child_ctx->dbg = NULL;
  child_ctx->state = WAITING;
  parent_context = get_ctx_by_pid(child_ctx->ppid);
  parent_context->dbg->cpid = child_ctx->pid;
}

/******************************************************************************/

// !!!!! -------- 64 bit system so Stack address should increase/decrease by
// 8-bytes

// Any process can call this be called by any func as we
// are only reading the code here
u64 get_value_at_address(u64 addr) {
  // printk("request to get a value\n");
  u64 value;
  asm("mov (%1), %0;" : "=r"(value) : "r"(addr) : "memory");
  // printk("requested value returned\n");
  return value;
}

// only kernel mode
void set_value_at_address(u64 addr, u64 value) {
  // printk("request to set a value\n");
  asm("mov %1, (%0);" : : "r"(addr), "r"(value) : "memory");
  // printk("value set successfully\n");
}
// remove all the functions which are not on the call stack
void prune_call_stack() {
  printk("someone wants to prune call stack\n");
  struct exec_context *child_context, *parent_context;
  struct debug_info *dbg_info;
  struct stack_func_info *curr, *caller, *last_caller;

  child_context = get_current_ctx();
  parent_context = get_ctx_by_pid(child_context->ppid);
  dbg_info = parent_context->dbg;

  curr = dbg_info->call_stack;
  if (curr == NULL) return;
  caller = curr->caller;
  // remove current
  free_stack_func_info(curr);
  curr = caller;

  while (curr != NULL && curr->bp != 1) {
    caller = curr->caller;
    free_stack_func_info(curr);
    curr = caller;
  }

  // update head
  dbg_info->call_stack = curr;
}

// !!! Handler main() properly
void update_call_stack() {
  printk("someone wants to update call stack\n");
  struct exec_context *child_context, *parent_context;
  struct debug_info *dbg_info;
  u64 entry_address, last_ret_addr, curr_ret_addr, curr_addr, curr_rbp;
  struct stack_func_info *curr, *caller, *last_caller;

  child_context = get_current_ctx();
  parent_context = get_ctx_by_pid(child_context->ppid);
  dbg_info = parent_context->dbg;

  // atleast main is on stack always

  // ! updates happen only at breakpoints
  if (dbg_info->call_stack->bp != 1) prune_call_stack();

  last_ret_addr = dbg_info->call_stack->ret_address;
  curr_rbp = child_context->regs.rbp;
  curr_addr = child_context->regs.entry_rip;
  curr_ret_addr = get_value_at_address(curr_rbp + 8);

  curr = alloc_stack_func_info();
  last_caller = dbg_info->call_stack;
  caller = last_caller;
  curr->addr = curr_addr;
  curr->ret_address = curr_ret_addr;
  curr->bp = 1;  // has a breakpoint
  dbg_info->call_stack = curr;

  printk("starting to create the call stack list\n");
  // starts from the function one above current
  while (1) {
    if (caller->caller == NULL) {
      printk("reached the end of list \n");
      curr->caller = last_caller;
      return;
    }
    // rbp contains the stack address containing the previous rbp
    curr_rbp = get_value_at_address(curr_rbp);
    curr_ret_addr = get_value_at_address(curr_rbp + 8);

    if (curr_ret_addr == last_ret_addr) {
      printk("reached the end of call stack list\n");
      curr->caller = last_caller;
      break;
    }

    caller = alloc_stack_func_info();
    caller->ret_address = curr_ret_addr;
    caller->bp = 0;
    curr->caller = caller;
    curr = caller;
  }

  // we are done
}

// get the information about a breakpoint
// ! better pass the head of list
void get_breakpoint_info(struct breakpoint_info *head, void *addr,
                         struct breakpoint_info **curr,
                         struct breakpoint_info **prev) {
  // addr = (u64)addr;
  *curr = NULL, *prev = NULL;
  if (head == NULL) return;
  if (head->addr == addr) {
    *curr = head;
  } else {
    while (head->next != NULL && head->next->addr != addr) {
      head = head->next;
    }
    *curr = head->next;
    *prev = head;
  }
  return;
}
// head is non-null
void insert_new_breakpoint(struct breakpoint_info *head,
                           struct breakpoint_info *new_breakpoint) {
  if (head->next == NULL)
    head->next = new_breakpoint;
  else
    insert_new_breakpoint(head->next, new_breakpoint);
}

// some breakpoint was on function at addr for sure
int is_on_stack(struct stack_func_info *call_stack, u64 addr) {
  if (call_stack == NULL) return 0;
  if ((call_stack->bp == 1) && (call_stack->addr == addr)) return 1;
  return is_on_stack(call_stack->caller, addr);
}
/* This is the int 0x3 handler
 * Hit from the childs context
 */

long int3_handler(struct exec_context *ctx) {
  struct exec_context *parent_ctx;
  struct debug_info *dbg_info;
  struct breakpoint_info *curr, *prev;
  u64 entry_addr, end_handler, curr_rbp, ret_addr, rip_value;

  parent_ctx = get_ctx_by_pid(ctx->ppid);
  dbg_info = parent_ctx->dbg;
  curr = NULL;
  prev = NULL;
  printk("child hits int3_handler\n");
  // Size of INT3 is 1 byte
  ctx->regs.entry_rip -= 1;
  // rip_value = ctx->regs.entry_rip;
  // asm("mov %0,%%rip;" : : "r"(rip_value) : "memory");

  // if (debug_info == NULL) return -1;

  entry_addr = ctx->regs.entry_rip;
  end_handler = ctx->dbg->end_handler;
  curr_rbp = ctx->regs.rbp;

  //  Fix current instruction and place breakpoint on the
  // previous place again
  if (dbg_info->mode == SINGLE_STEP) {
    printk("debugger raised error to restore breakpoints\n");
    if (entry_addr == end_handler + 1) {
      printk("fixing end_handler()\n");
      // remove INT3
      set_value_at_address(entry_addr, dbg_info->end_second);
      printk("removed INT3 inserted before");
      // insert INT3
      printk("inserting breakpoint at end handler again\n");
      set_value_at_address(
          end_handler,
          (dbg_info->end_first & 0xFFFFFFFFFFFFFF00) | INT3_OPCODE);
      printk("success\n");
    } else {
      printk("fixing start of function\n");
      get_breakpoint_info(dbg_info->head, entry_addr - 1, &curr, &prev);
      // remove INT3
      set_value_at_address(entry_addr, curr->second_inst);
      printk("removed INT3 inserted before");

      // if breakpoint is still enabled, recover it
      if (curr->end_breakpoint_enable) {
        printk("inserting breakpoint again\n");
        set_value_at_address(
            entry_addr - 1,
            (dbg_info->end_first & 0xFFFFFFFFFFFFFF00) | INT3_OPCODE);
        printk("success\n");
      }
    }

    // child should continue;
    dbg_info->mode = DONE;
    ctx->state = READY;
    schedule(ctx);
    // return 0;
  }

  // Now Function can hit from two locations:

  // 1. Start of the function
  if (entry_addr != end_handler) {
    printk("debugge hits the breakpoint at start of function\n");
    update_call_stack();
    get_breakpoint_info(dbg_info->head, entry_addr, &curr, &prev);

    if (curr->end_breakpoint_enable == 1) {
      // return address is changed, prev_rbp is not
      set_value_at_address(curr_rbp + 8, end_handler);
      // set_value_at_address(curr_rbp, curr_rbp);
    }
  }
  // 2. End of function
  else {
    printk("child hits breakpoint at end\n");
    // the function will be at the end of call_stack
    ret_addr = dbg_info->call_stack->ret_address;
    prune_call_stack();

    // push the return address, stack can be changed freely
    asm volatile(
        "push %0;"
        "push %1;"
        "mov %%rsp, %%rbp;"
        :
        : "r"(ret_addr), "r"(curr_rbp)
        : "memory");
  }

  // Schedule the debugger
  ctx->state = WAITING;
  parent_ctx->state = READY;
  dbg_info->mode = RECORD;
  schedule(parent_ctx);
}

/*
 * Exit handler.
 * Deallocate the debug_info struct if its a debugger.
 * Wake up the debugger if its a child
 */
void debugger_on_exit(struct exec_context *ctx) {
  struct exec_context *parent_ctx = get_ctx_by_pid(ctx->ppid);

  // debugger
  if (ctx != NULL && ctx->dbg != NULL) {
    // cleaning
    printk("debugger trying to exit\n");
    set_value_at_address(ctx->dbg->end_handler, ctx->dbg->end_first);
    free_stack_func_info(ctx->dbg->call_stack);
    free_breakpoint_info_list(ctx->dbg->head);
    free_debug_info(ctx->dbg);
    return;
  }
  // debugge
  if (parent_ctx != NULL && parent_ctx->dbg != NULL) {
    printk("debugee trying to exit\n");
    parent_ctx->dbg->cpid = -1;
    parent_ctx->state = READY;
    // schedule(parent_ctx);
  }
}
/*
 * called from debuggers context
 * initializes debugger state
 */
int do_become_debugger(struct exec_context *ctx, void *addr) {
  // Your code

  if (ctx == NULL) return -1;
  u64 end_first, end_second;

  end_first = get_value_at_address(addr);
  end_second = get_value_at_address(addr + 1);
  // initial breakpoint at end_address
  // must be called only from child context
  set_value_at_address(addr, (end_first & 0xFFFFFFFFFFFFFF00) | INT3_OPCODE);

  ctx->dbg = alloc_debug_info();
  ctx->dbg->end_handler = addr;
  ctx->dbg->breakpoint_count = 0;
  ctx->dbg->last_id = 0;
  ctx->dbg->head = NULL;
  ctx->dbg->mode = DONE;
  ctx->dbg->end_first = end_first;
  ctx->dbg->end_second = end_second;

  // main is currently on stack for sure
  // ! A breakpoint is set on the start of main()
  ctx->dbg->call_stack = alloc_stack_func_info();
  // Check again
  ctx->dbg->call_stack->bp = 1;
  ctx->dbg->call_stack->addr = NULL;
  ctx->dbg->call_stack->ret_address = END_ADDR;
  ctx->dbg->call_stack->caller = NULL;
  return 0;
}

/*
 * called from debuggers context
 */
int do_set_breakpoint(struct exec_context *ctx, void *addr, int flag) {
  // Your code
  printk("Trying to set breakpoint\n");
  if (ctx == NULL || ctx->dbg == NULL) return -1;

  struct breakpoint_info *head, *prev, *head_addr, *new_breakpoint;
  u64 first_inst, second_inst;

  get_breakpoint_info(ctx->dbg->head, addr, &head, &prev);

  if (head != NULL) {
    // No change requested
    if (head->end_breakpoint_enable == flag) return 0;

    // you cannot set/unset flag of a function which is on stack
    if (is_on_stack(ctx->dbg->call_stack, addr)) return -1;

    // if not on stack Change flag and Return 0.
    head->end_breakpoint_enable = flag;
    return 0;
  }

  // non-breakpointed function

  if (ctx->dbg->breakpoint_count == MAX_BREAKPOINTS) {
    printk("No more breakpoints can be inserted, Limit Reached!");
    return -1;
  }

  // The breakpoint number is incremented if and only if set breakpoint was
  // successful.
  ctx->dbg->breakpoint_count++;
  ctx->dbg->last_id++;
  printk("Current Address %x\n", addr);
  first_inst = get_value_at_address(addr);
  printf("The first instruction before changing was %llx\n", first_inst);
  second_inst = get_value_at_address(addr + 1);
  set_value_at_address(addr, (first_inst & 0xFFFFFFFFFFFFFF00) | INT3_OPCODE);
  printf("The first instruction after changing was %llx\n",
         get_value_at_address(addr));

  new_breakpoint = alloc_breakpoint_info();
  new_breakpoint->addr = addr;
  new_breakpoint->num = ctx->dbg->last_id;
  new_breakpoint->next = NULL;
  new_breakpoint->end_breakpoint_enable = flag;
  new_breakpoint->first_inst = first_inst;
  new_breakpoint->second_inst = second_inst;

  head = ctx->dbg->head;
  if (head == NULL)
    ctx->dbg->head = new_breakpoint;
  else
    insert_new_breakpoint(head, new_breakpoint);
  return 0;
}

/*
 * called from debuggers context
 */
int do_remove_breakpoint(struct exec_context *ctx, void *addr) {
  // Your code
  struct breakpoint_info *head, *curr, *prev;
  // You are required to find the breakpoint entry corresponding to the address
  // specified by addr in list of breakpoints pointed to by head which is
  // accessible from dbg pointer which is part of the exec context structure.
  head = ctx->dbg->head;
  curr = NULL;
  prev = NULL;
  get_breakpoint_info(head, addr, &curr, &prev);

  // If no breakpoint corresponding addr exists, return -1 (error).
  if (curr == NULL) {
    printk("No breakpoint on given address\n");
    return -1;
  }

  // If remove breakpoint is called on a function that is currently active on
  // the call stack of debuggee process (function has been called but not
  // returned yet), AND its end breakpoint enable flag is set to the value 1,
  // then also remove breakpoint should return -1 (error).
  if (is_on_stack(curr, addr) && curr->end_breakpoint_enable) return -1;

  // valid address
  // You have to completely remove the information about this breakpoint that
  // you have maintained
  ctx->dbg->breakpoint_count--;

  // Also, make sure that when in the future the child process’s execution
  // reaches addr, INT3 shouldn’t be generated anymore.
  set_value_at_address(addr, curr->first_inst);
  if (prev == NULL)
    ctx->dbg->head = curr->next;
  else
    prev->next = curr->next;
  free_breakpoint_info(curr);
  return 0;
}

/*
 * called from debuggers context
 */

int do_info_breakpoints(struct exec_context *ctx, struct breakpoint *ubp) {
  // Your code
  struct breakpoint_info *head;
  u32 count;

  if (ctx == NULL || ctx->dbg == NULL) return -1;

  count = ctx->dbg->breakpoint_count;
  head = ctx->dbg->head;

  if (head == NULL) return -1;

  for (int i = 0; i < count; i++) {
    if (head == NULL) return -1;
    ubp[i].addr = head->addr;
    ubp[i].end_breakpoint_enable = head->end_breakpoint_enable;
    ubp[i].num = head->num;
    head = head->next;
  }
  return count;
}

/*
 * called from debuggers context
 */
int do_info_registers(struct exec_context *ctx, struct registers *regs) {
  // Your code
  // info registers is used to find the values present in registers just before
  // a breakpoint occurs during the execution of debuggee. Address of a struct
  // register type variable will be passed from user space as the argument of
  // info registers. You are required to fill the register values of child
  // process just before the INT3 instruction gets executed (i.e. just before
  // breakpoint occurred) into this structure passed as argument.

  // !!! After executing INT3, only RIP should change, as kernel takes control
  // after that instruction, does the job and restores the user regs ! Need to
  // confirm
  regs->entry_rip = ctx->regs.entry_rip;
  regs->entry_rsp = ctx->regs.entry_rsp;
  regs->rbp = ctx->regs.rbp;
  regs->rax = ctx->regs.rax;
  regs->rdi = ctx->regs.rdi;
  regs->rsi = ctx->regs.rsi;
  regs->rcx = ctx->regs.rcx;
  regs->r8 = ctx->regs.r8;
  regs->r9 = ctx->regs.r9;
  return 0;
}

/*
 * Called from debuggers context
 */
int do_backtrace(struct exec_context *ctx, u64 bt_buf) {
  // Your code
  struct stack_func_info *call_stack;
  u64 ret_addr, i;
  u64 *bt;

  bt = (u64 *)bt_buf;
  call_stack = ctx->dbg->call_stack;

  if (call_stack == NULL) {
    printk("Call stack not updated properly\n");
    return -1;
  }

  i = 0;
  ret_addr = call_stack->ret_address;

  // if called from start of a function store its
  // address too
  if (call_stack->bp) {
    bt[0] = call_stack->addr;
    i++;
  }

  while (ret_addr != END_ADDR) {
    bt[i] = ret_addr;
    i++;
    call_stack = call_stack->caller;
    if (call_stack == NULL) break;
    ret_addr = call_stack->ret_address;
  }

  return i;
}

/*
 * When the debugger calls wait
 * it must move to WAITING state
 * and its child must move to READY state
 */

s64 do_wait_and_continue(struct exec_context *ctx) {
  // Five ways to reach here:
  // 1. Initially set breakpoints while child has not yet started
  //      -> Child should continue unconditionally
  // 2. Child hits a breakpoint : CHILD WAITS
  //      -> Debugger should observe the child process, returning
  //         the address of breakpoint and continue itself
  // 3. Child exits
  //      -> Debugger should return CHILD_EXIT and continue itself
  // 4. Debugger is done observing the registers and now want the
  //    Child to continue
  //      ->  prepare the child for normal execution and shedule
  if (ctx == NULL) return -1;

  struct exec_context *child_context;
  struct debug_info *dbg_info;
  u64 curr_addr, curr_instr;
  u32 cpid;

  cpid = ctx->dbg->cpid;
  if (cpid != -1) {
    child_context = get_ctx_by_pid(ctx->dbg->cpid);
    curr_addr = child_context->regs.entry_rip;
    curr_instr = get_value_at_address(curr_addr);
    dbg_info = ctx->dbg;
  }

  // Case 3
  if (cpid == -1) {
    printk("Child exited\n");
    ctx->regs.rax = CHILD_EXIT;
  }
  // only first time
  else if (dbg_info->mode == DONE) {
    // confirm this
    printk("First time child entry\n");
    ctx->regs.rax = curr_addr;
  }

  // breakpoint
  else if ((curr_instr & 0xFF) == INT3_OPCODE) {
    // if (dbg_info->mode == RECORD) {
    dbg_info->mode = SINGLE_STEP;
    printk("Single step child\n");

    if (curr_addr == dbg_info->end_handler) {
      // fix current address
      set_value_at_address(curr_addr, dbg_info->end_first);
      // place breakpoint at next instruction
      // assuming push %rbp has size = 1
      set_value_at_address(
          curr_addr + 1,
          (dbg_info->end_second & 0xFFFFFFFFFFFFFF00) | INT3_OPCODE);
    } else {
      struct breakpoint_info *curr, *prev;
      get_breakpoint_info(dbg_info->head, curr_addr, &curr, &prev);
      set_value_at_address(curr_addr, curr->first_inst);
      set_value_at_address(
          curr_addr + 1,
          (curr->second_inst & 0xFFFFFFFFFFFFFF00) | INT3_OPCODE);
    }
    // }
  } else {
    ctx->regs.rax = -1;
  }

  // return value
  ctx->state = WAITING;
  child_context->state = READY;
  schedule(child_context);

  // // should not reach here
  return -1;
}
