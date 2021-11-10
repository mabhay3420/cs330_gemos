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
  free_breakpoint_info(ptr);
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

// get the information about a breakpoint
void get_breakpoint_info(struct breakpoint_info *head, void *addr,
                         struct breakpoint_info **curr,
                         struct breakpoint_info **prev) {
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

// some breakpoint was on function at addr for sure
int is_on_stack(struct stack_func_info *call_stack, u64 addr) {
  if (call_stack == NULL) return 0;
  if (call_stack->addr == addr) return 1;
  return is_on_stack(call_stack->caller, addr);
}
/* This is the int 0x3 handler
 * Hit from the childs context
 */

long int3_handler(struct exec_context *ctx) {
  struct exec_context *parent_ctx;
  struct debug_info *dbg_info;
  struct breakpoint_info *curr, *prev;
  u64 entry_addr, end_handler, ret_addr;

  parent_ctx = get_ctx_by_pid(ctx->ppid);
  dbg_info = parent_ctx->dbg;
  curr = NULL;
  prev = NULL;
  // Size of INT3 is 1 byte
  entry_addr = ctx->regs.entry_rip - 1;
  end_handler = dbg_info->end_handler;
  get_breakpoint_info(dbg_info->head, entry_addr, &curr, &prev);

  u32 i = 0;
  u64 *bt = parent_ctx->dbg->backtrace;

  // 1. Start of the function
  if (entry_addr != end_handler) {
    struct stack_func_info *new_stack_info = alloc_stack_func_info();
    ret_addr = *((u64 *)ctx->regs.entry_rsp);
    new_stack_info->addr = entry_addr;
    new_stack_info->ret_address = ret_addr;
    new_stack_info->caller = dbg_info->call_stack;
    dbg_info->call_stack = new_stack_info;

    // store current information
    bt[i] = entry_addr;
    i++;

    // manual push
    ctx->regs.entry_rsp -= 8;
    *((u64 *)(ctx->regs.entry_rsp)) = ctx->regs.rbp;

    if (curr->end_breakpoint_enable) {
      *((u64 *)ctx->regs.entry_rsp + 1) = end_handler;
    }

  }
  // 2. End of function
  else {
    dbg_info->call_stack->addr = entry_addr;  // so that they know who has come
    ret_addr = parent_ctx->dbg->call_stack->ret_address;

    ctx->regs.entry_rsp -= 16;
    *((u64 *)ctx->regs.entry_rsp) = ctx->regs.rbp;
    *((u64 *)ctx->regs.entry_rsp + 1) = ret_addr;
  }

  u64 *curr_rbp = (u64 *)ctx->regs.entry_rsp;
  ret_addr = *(curr_rbp + 1);
  struct stack_func_info *call_stack = parent_ctx->dbg->call_stack;

  // will be pruned anyway
  if (entry_addr == end_handler) {
    call_stack = call_stack->caller;
  } else if (curr->end_breakpoint_enable != 1) {
    call_stack = call_stack->caller;
  }
  while (i < MAX_BACKTRACE && ret_addr != END_ADDR) {
    if (ret_addr == parent_ctx->dbg->end_handler) {
      // printk("Imposter\n");
      ret_addr = call_stack->ret_address;
      call_stack = call_stack->caller;
    }
    bt[i] = ret_addr;
    i++;
    curr_rbp = (u64 *)(*curr_rbp);
    ret_addr = *(curr_rbp + 1);
  }

  // marks the ending
  bt[i] = -1;

  // Schedule the debugger
  ctx->state = WAITING;
  parent_ctx->regs.rax = entry_addr;
  parent_ctx->state = READY;
  schedule(parent_ctx);
}

/*
 * Exit handler.
 * Deallocate the debug_info struct if its a debugger.
 * Wake up the debugger if its a child
 */
void debugger_on_exit(struct exec_context *ctx) {
  struct exec_context *parent_ctx;
  if (ctx->dbg == NULL) {
    parent_ctx = get_ctx_by_pid(ctx->ppid);
  }

  // debugger
  if (ctx != NULL && ctx->dbg != NULL) {
    *((u32 *)ctx->dbg->end_handler) = ctx->dbg->end_first;
    // should be null anyway
    free_stack_func_info(ctx->dbg->call_stack);
    free_breakpoint_info_list(ctx->dbg->head);
    free_debug_info(ctx->dbg);
    return;
  }
  // debugge
  if (parent_ctx != NULL && parent_ctx->dbg != NULL) {
    parent_ctx->dbg->cpid = -1;
    parent_ctx->state = READY;
  }
}
/*
 * called from debuggers context
 * initializes debugger state
 */
int do_become_debugger(struct exec_context *ctx, void *addr) {
  // Your code

  if (ctx == NULL) return -1;
  u32 end_first, end_second;

  end_first = *((u32 *)addr);
  *((u32 *)addr) = (end_first & 0xFFFFFF00) | INT3_OPCODE;

  ctx->dbg = alloc_debug_info();
  ctx->dbg->end_handler = addr;
  ctx->dbg->breakpoint_count = 0;
  ctx->dbg->last_id = 0;
  ctx->dbg->head = NULL;
  ctx->dbg->end_first = end_first;

  return 0;
}

/*
 * called from debuggers context
 */
int do_set_breakpoint(struct exec_context *ctx, void *addr, int flag) {
  if (ctx == NULL || ctx->dbg == NULL) return -1;

  struct breakpoint_info *curr, *prev, *head, *new_breakpoint;
  u32 first_inst;
  head = ctx->dbg->head;

  get_breakpoint_info(head, addr, &curr, &prev);

  if (curr != NULL) {
    if (curr->end_breakpoint_enable == flag) return 0;
    if (is_on_stack(ctx->dbg->call_stack, addr)) {
      return -1;
    }
    curr->end_breakpoint_enable = flag;
    return 0;
  }

  if (ctx->dbg->breakpoint_count == MAX_BREAKPOINTS) {
    return -1;
  }

  ctx->dbg->breakpoint_count++;
  ctx->dbg->last_id++;
  first_inst = *((u32 *)addr);
  *((u32 *)addr) = (first_inst & 0xFFFFFF00) | INT3_OPCODE;

  new_breakpoint = alloc_breakpoint_info();
  new_breakpoint->addr = addr;
  new_breakpoint->num = ctx->dbg->last_id;
  new_breakpoint->next = NULL;
  new_breakpoint->end_breakpoint_enable = flag;

  if (head == NULL) {
    ctx->dbg->head = new_breakpoint;
  } else {
    while (head->next != NULL) head = head->next;
    head->next = new_breakpoint;
  }
  return 0;
}

/*
 * called from debuggers context
 */
int do_remove_breakpoint(struct exec_context *ctx, void *addr) {
  // Your code
  struct breakpoint_info *head, *curr, *prev;
  head = ctx->dbg->head;
  curr = NULL;
  prev = NULL;
  get_breakpoint_info(head, addr, &curr, &prev);

  if (curr == NULL) {
    return -1;
  }

  if (is_on_stack(ctx->dbg->call_stack, addr) && curr->end_breakpoint_enable) {
    // printk("Function is on stack and its end point is enabled\n");
    return -1;
  }

  ctx->dbg->breakpoint_count--;

  // set_value_at_address(addr, curr->first_inst);
  *((u32 *)addr) = curr->first_inst;
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
  struct exec_context *child_ctx = get_ctx_by_pid(ctx->dbg->cpid);
  regs->entry_rip =
      child_ctx->regs.entry_rip - 1;  // see if there is some other way

  regs->entry_rsp = child_ctx->regs.entry_rsp + 8;
  regs->rbp = child_ctx->regs.rbp;  // has not been updated by us, will be
                                    // updated in next instruction
  regs->rax = child_ctx->regs.rax;
  regs->rdi = child_ctx->regs.rdi;
  regs->rdx = child_ctx->regs.rdx;
  regs->rsi = child_ctx->regs.rsi;
  regs->rcx = child_ctx->regs.rcx;
  regs->r8 = child_ctx->regs.r8;
  regs->r9 = child_ctx->regs.r9;
  return 0;
}

/*
 * Called from debuggers context
 */
int do_backtrace(struct exec_context *ctx, u64 bt_buf) {
  u64 *bt = (u64 *)bt_buf;
  u64 *storage = ctx->dbg->backtrace;
  u32 i = 0;
  while (i < MAX_BACKTRACE && storage[i] != -1) {
    bt[i] = storage[i];
    i++;
  }
  return i;
}

/*
 * When the debugger calls wait
 * it must move to WAITING state
 * and its child must move to READY state
 */

s64 do_wait_and_continue(struct exec_context *ctx) {
  if (ctx == NULL) return -1;
  u32 cpid = ctx->dbg->cpid;
  if (cpid == -1) return CHILD_EXIT;

  struct exec_context *child_context;
  struct breakpoint_info *curr, *prev;
  struct stack_func_info *call_stack;
  u64 curr_addr;
  child_context = get_ctx_by_pid(cpid);

  // either call stack is empty or we have a breakpoint
  if (ctx->dbg->call_stack == NULL) {
    // printk("Call from Main\n");
    // ? What to return
  } else {
    call_stack = ctx->dbg->call_stack;
    curr_addr = call_stack->addr;
    if (curr_addr == ctx->dbg->end_handler) {
      ctx->dbg->call_stack = call_stack->caller;
      free_stack_func_info(call_stack);
    } else {
      get_breakpoint_info(ctx->dbg->head, curr_addr, &curr, &prev);
      if (curr->end_breakpoint_enable != 1) {
        ctx->dbg->call_stack = call_stack->caller;
        free_stack_func_info(call_stack);
      }
    }
    // no need here
    // ctx->regs.rax = curr_addr;
  }

  ctx->state = WAITING;
  child_context->state = READY;
  schedule(child_context);

  // // should not reach here
  return -1;
}
