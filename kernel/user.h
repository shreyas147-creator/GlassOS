#ifndef USER_H
#define USER_H

#include "types.h"

void user_init(void);
int user_run_demo(void);
void user_syscall_trap(uint64_t number, uint64_t user_cs,
                       uint64_t arg0, uint64_t arg1, uint64_t arg2);
int user_handle_fault(uint64_t vector, uint64_t error_code, void *frame);

#endif
