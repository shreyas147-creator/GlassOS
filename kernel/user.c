#include "user.h"
#include "config.h"
#include "event.h"
#include "memory.h"
#include "object.h"
#include "policy.h"
#include "vga.h"

#define USER_PROCESS_CAPACITY 4
#define USER_NAME_SIZE 48

struct user_process {
    uint32_t pid;
    uint32_t thread_id;
    char process_name[USER_NAME_SIZE];
    char thread_name[USER_NAME_SIZE];
};

static struct user_process processes[USER_PROCESS_CAPACITY];
static uint32_t process_count;
static uint32_t scheduler_index;
static struct user_process *active_user_process;
static int user_fault_contained;

uint64_t user_kernel_return_rsp;
uint64_t user_kernel_return_rip;

extern void user_enter_ring3(uint64_t entry, uint64_t stack);

struct user_interrupt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

static const uint8_t user_program[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,       /* mov eax, 1 ; inspect */
    0xCD, 0x80,                         /* int 0x80 */
    0xB8, 0x02, 0x00, 0x00, 0x00,       /* mov eax, 2 ; yield */
    0xCD, 0x80,
    0xB8, 0x03, 0x00, 0x00, 0x00,       /* mov eax, 3 ; config_get */
    0xCD, 0x80,
    0xB8, 0x04, 0x00, 0x00, 0x00,       /* mov eax, 4 ; denied config_set */
    0xCD, 0x80,
    0x48, 0xA1, 0x00, 0x00, 0x00, 0x00, /* mov rax, [0] ; supervisor page fault */
    0x00, 0x00, 0x00, 0x00
};

static void append_text(char *destination, uint32_t capacity, uint32_t *position, const char *text) {
    while (*text != '\0' && *position < capacity - 1) {
        destination[*position] = *text;
        (*position)++;
        text++;
    }
    destination[*position] = '\0';
}

static void append_decimal(char *destination, uint32_t capacity, uint32_t *position, uint32_t value) {
    char digits[10];
    uint32_t count = 0;
    if (value == 0) {
        append_text(destination, capacity, position, "0");
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count != 0) {
        char single[2];
        single[0] = digits[--count];
        single[1] = '\0';
        append_text(destination, capacity, position, single);
    }
}

static void build_name(char *destination, uint32_t capacity, const char *prefix, uint32_t id) {
    uint32_t position = 0;
    append_text(destination, capacity, &position, prefix);
    append_decimal(destination, capacity, &position, id);
}

void user_init(void) {
    process_count = 0;
    scheduler_index = 0;
    object_register("scheduler", "scheduler", object_id("system"), "cooperative", event_current());
    object_register("user_transition", "user_transition", object_id("scheduler"),
                    "scaffold-ready", event_current());
    event_record_kind("scheduler", "scheduler", "initialize", EVENT_OK,
                      event_current(), "allow:boot-user", "state");
}

static struct user_process *create_process(void) {
    if (process_count >= USER_PROCESS_CAPACITY) {
        event_record_kind("scheduler", "scheduler", "process_create", EVENT_DENIED,
                          event_current(), "deny:process-table-full", "state");
        return 0;
    }

    struct user_process *process = &processes[process_count];
    process->pid = process_count + 1;
    process->thread_id = process_count + 1;
    build_name(process->process_name, sizeof(process->process_name), "process/", process->pid);
    build_name(process->thread_name, sizeof(process->thread_name), "thread/", process->thread_id);
    process_count++;

    uint32_t scheduler = object_id("scheduler");
    uint32_t process_object = object_register("process", process->process_name,
                                              scheduler, "created", event_current());
    object_register("thread", process->thread_name, process_object,
                    "runnable", event_current());
    event_record_kind("scheduler", process->process_name, "process_create", EVENT_OK,
                      event_current(), "allow:user-process", "state");
    event_record_kind("scheduler", process->thread_name, "thread_create", EVENT_OK,
                      event_current(), "allow:user-thread", "state");
    printf("Created %s with %s\n", process->process_name, process->thread_name);
    return process;
}

static void transition_scaffold(const struct user_process *process) {
    event_record_kind("scheduler", process->thread_name, "user_transition_scaffold",
                      EVENT_OK, event_current(), "allow:user-transition", "state");
    printf("User transition scaffold ready for %s\n", process->thread_name);
}

static void copy_user_program(uint8_t *destination) {
    for (uint32_t i = 0; i < 4096; i++) {
        destination[i] = 0x90;
    }
    for (uint32_t i = 0; i < (uint32_t)sizeof(user_program); i++) {
        destination[i] = user_program[i];
    }
}

static uint64_t syscall_begin(const struct user_process *process, const char *target) {
    uint64_t parent = event_current();
    uint64_t transaction = transaction_begin(process->thread_name, target, parent);
    event_set_transaction(transaction);
    event_set_current(transaction == 0 ? parent : transaction);
    return transaction;
}

static void syscall_finish(uint64_t transaction, int ok) {
    if (transaction != 0) {
        if (ok) {
            transaction_commit(transaction);
        } else {
            transaction_abort(transaction, "deny:syscall");
        }
    }
    event_set_transaction(0);
}

static int user_syscall_inspect(const struct user_process *process, const char *target) {
    uint64_t transaction = syscall_begin(process, target);
    struct policy_decision policy = policy_check(process->thread_name, target, "syscall:inspect");
    if (!policy.allowed) {
        printf("User syscall denied: %s\n", policy.reason);
        syscall_finish(transaction, 0);
        return 0;
    }
    event_record_kind(process->thread_name, target, "inspect", EVENT_OK,
                      policy.event, policy.reason, "syscall");
    object_inspect(target);
    syscall_finish(transaction, 1);
    return 1;
}

static int user_syscall_yield(const struct user_process *process) {
    uint64_t transaction = syscall_begin(process, process->thread_name);
    struct policy_decision policy = policy_check(process->thread_name, process->thread_name, "syscall:yield");
    if (!policy.allowed) {
        printf("User syscall denied: %s\n", policy.reason);
        syscall_finish(transaction, 0);
        return 0;
    }

    scheduler_index = process_count == 0 ? 0 : (scheduler_index + 1) % process_count;
    event_record_kind(process->thread_name, "scheduler", "yield", EVENT_OK,
                      policy.event, policy.reason, "syscall");
    event_record_kind("scheduler", process->thread_name, "schedule", EVENT_OK,
                      policy.event, policy.reason, "state");
    printf("%s yielded cooperatively\n", process->thread_name);
    syscall_finish(transaction, 1);
    return 1;
}

static int user_syscall_config_get(const struct user_process *process, const char *key) {
    uint64_t transaction = syscall_begin(process, key);
    struct policy_decision policy = policy_check(process->thread_name, key, "syscall:config_get");
    if (!policy.allowed) {
        printf("User syscall denied: %s\n", policy.reason);
        syscall_finish(transaction, 0);
        return 0;
    }

    const char *value = config_get(key);
    if (value == 0) {
        event_record_kind(process->thread_name, key, "config_get", EVENT_DENIED,
                          policy.event, "deny:unknown-config-key", "syscall");
        syscall_finish(transaction, 0);
        return 0;
    }

    event_record_kind(process->thread_name, key, "config_get", EVENT_OK,
                      policy.event, policy.reason, "syscall");
    printf("user syscall config_get %s=%s\n", key, value);
    syscall_finish(transaction, 1);
    return 1;
}

static int user_syscall_config_set_denied(const struct user_process *process, const char *key) {
    uint64_t transaction = syscall_begin(process, key);
    struct policy_decision policy = policy_check(process->thread_name, key, "syscall:config_set");
    if (!policy.allowed) {
        printf("User syscall denied: %s\n", policy.reason);
        syscall_finish(transaction, 0);
        return 0;
    }

    event_record_kind(process->thread_name, key, "config_set", EVENT_OK,
                      policy.event, policy.reason, "syscall");
    syscall_finish(transaction, 1);
    return 1;
}

int user_run_demo(void) {
    struct user_process *process = create_process();
    if (process == 0) {
        return 0;
    }

    transition_scaffold(process);
    uint64_t code_page = page_alloc();
    uint64_t stack_page = page_alloc();
    if (code_page == 0 || stack_page == 0 ||
        !memory_map_user_page(code_page, 1) ||
        !memory_map_user_page(stack_page, 0)) {
        printf("User demo failed: page setup\n");
        return 0;
    }

    copy_user_program((uint8_t *)code_page);
    active_user_process = process;
    user_fault_contained = 0;
    event_record_kind("scheduler", process->thread_name, "enter_cpl3",
                      EVENT_OK, event_current(), "allow:user-enter", "state");
    user_enter_ring3(code_page, stack_page + 4096);
    active_user_process = 0;
    printf("User demo complete: cpl3=yes syscall_return=yes fault_contained=%s\n",
           user_fault_contained ? "yes" : "no");
    return user_fault_contained;
}

void user_syscall_trap(uint64_t number, uint64_t user_cs) {
    if (active_user_process == 0) {
        event_record_kind("cpu", "syscall", "trap", EVENT_DENIED,
                          event_current(), "deny:no-active-user", "syscall");
        return;
    }

    event_record_kind(active_user_process->thread_name, "kernel", "cpl3_trap",
                      (user_cs & 3ULL) == 3 ? EVENT_OK : EVENT_DENIED,
                      event_current(),
                      (user_cs & 3ULL) == 3 ? "allow:cpl3" : "deny:not-cpl3",
                      "syscall");

    if (number == 1) {
        user_syscall_inspect(active_user_process, "system");
    } else if (number == 2) {
        user_syscall_yield(active_user_process);
    } else if (number == 3) {
        user_syscall_config_get(active_user_process, "/config/theme");
    } else if (number == 4) {
        user_syscall_config_set_denied(active_user_process, "/config/theme");
    } else {
        uint64_t transaction = syscall_begin(active_user_process, "kernel");
        event_record_kind(active_user_process->thread_name, "kernel", "unknown_syscall",
                          EVENT_DENIED, event_current(), "deny:unknown-syscall", "syscall");
        syscall_finish(transaction, 0);
    }
}

int user_handle_fault(uint64_t vector, uint64_t error_code, void *frame) {
    struct user_interrupt_frame *interrupt_frame = (struct user_interrupt_frame *)frame;
    if (active_user_process == 0 || vector != 14 || (interrupt_frame->cs & 3ULL) != 3) {
        return 0;
    }

    user_fault_contained = 1;
    event_record_kind(active_user_process->thread_name, "kernel", "page_fault",
                      EVENT_FAULT, event_current(), "deny:user-page-fault", "fault");
    printf("User page fault contained: rip=%x error=%x\n",
           interrupt_frame->rip, error_code);
    return 1;
}
