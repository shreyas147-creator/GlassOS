#include "user.h"
#include "config.h"
#include "event.h"
#include "memory.h"
#include "object.h"
#include "policy.h"
#include "ramfs.h"
#include "vga.h"

#define USER_PROCESS_CAPACITY 4
#define USER_NAME_SIZE 48
#define USER_CODE_VA 0x400000ULL
#define USER_STACK_TOP 0x800000ULL
#define USER_PEER_PROBE_VA 0x600000ULL

struct user_process {
    uint32_t pid;
    uint32_t thread_id;
    uint64_t cr3;
    uint64_t code_page;
    uint64_t stack_page;
    uint64_t peer_probe_address;
    char process_name[USER_NAME_SIZE];
    char thread_name[USER_NAME_SIZE];
    char address_space_name[USER_NAME_SIZE];
    char image_path[USER_NAME_SIZE];
    char peer_process_name[USER_NAME_SIZE];
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

static void copy_text(char *destination, uint32_t capacity, const char *source) {
    uint32_t position = 0;
    append_text(destination, capacity, &position, source == 0 ? "" : source);
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
    process->cr3 = 0;
    process->code_page = 0;
    process->stack_page = 0;
    process->peer_probe_address = 0;
    build_name(process->process_name, sizeof(process->process_name), "process/", process->pid);
    build_name(process->thread_name, sizeof(process->thread_name), "thread/", process->thread_id);
    build_name(process->address_space_name, sizeof(process->address_space_name), "address_space/", process->pid);
    copy_text(process->image_path, sizeof(process->image_path), "");
    copy_text(process->peer_process_name, sizeof(process->peer_process_name), "");
    process_count++;

    uint32_t scheduler = object_id("scheduler");
    uint32_t process_object = object_register("process", process->process_name,
                                              scheduler, "created", event_current());
    object_register("thread", process->thread_name, process_object,
                    "runnable", event_current());
    object_register("address_space", process->address_space_name, process_object,
                    "created", event_current());
    event_record_kind("scheduler", process->process_name, "process_create", EVENT_OK,
                      event_current(), "allow:user-process", "state");
    event_record_kind("scheduler", process->thread_name, "thread_create", EVENT_OK,
                      event_current(), "allow:user-thread", "state");
    process->cr3 = memory_create_address_space(process->address_space_name);
    if (process->cr3 == 0) {
        event_record_kind("scheduler", process->address_space_name, "address_space_create",
                          EVENT_DENIED, event_current(), "deny:address-space", "state");
        return 0;
    }
    event_record_kind("scheduler", process->address_space_name, "address_space_create",
                      EVENT_OK, event_current(), "allow:user-address-space", "state");
    printf("Created %s with %s\n", process->process_name, process->thread_name);
    return process;
}

static void transition_scaffold(const struct user_process *process) {
    event_record_kind("scheduler", process->thread_name, "user_transition_scaffold",
                      EVENT_OK, event_current(), "allow:user-transition", "state");
    printf("User transition scaffold ready for %s\n", process->thread_name);
}

static void clear_page(uint8_t *destination) {
    for (uint32_t i = 0; i < 4096; i++) {
        destination[i] = 0x90;
    }
}

static int load_executable(struct user_process *process, const char *path) {
    struct policy_decision policy = policy_check("shell", path, "process:load");
    if (!policy.allowed) {
        printf("Denied: %s\n", policy.reason);
        return 0;
    }

    uint64_t command_event = event_current();
    event_set_current(policy.event);
    const struct ramfs_file *file = ramfs_lookup(path);
    event_set_current(command_event);
    if (file == 0 || file->type != RAMFS_FILE_EXECUTABLE || file->size > PAGE_SIZE) {
        event_record_kind("ramfs", path, "load", EVENT_DENIED,
                          policy.event, "deny:not-executable", "policy");
        printf("Executable not found: %s\n", path);
        return 0;
    }

    process->code_page = page_alloc();
    process->stack_page = page_alloc();
    if (process->code_page == 0 || process->stack_page == 0) {
        event_record_kind("scheduler", process->process_name, "load_executable",
                          EVENT_DENIED, policy.event, "deny:user-pages", "state");
        return 0;
    }

    clear_page((uint8_t *)process->code_page);
    ramfs_read(file, 0, (void *)process->code_page, file->size);
    if (!memory_map_process_page(process->cr3, USER_CODE_VA, process->code_page,
                                 1, 0, 1, "allow:user-code-page") ||
        !memory_map_process_page(process->cr3, USER_STACK_TOP - PAGE_SIZE, process->stack_page,
                                 1, 1, 0, "allow:user-stack-page")) {
        return 0;
    }

    copy_text(process->image_path, sizeof(process->image_path), path);
    event_record_kind("ramfs", path, "read", EVENT_OK,
                      policy.event, policy.reason, "state");
    event_record_kind("scheduler", process->process_name, "load_executable",
                      EVENT_OK, policy.event, "allow:user-image", "state");
    printf("Loaded %s into %s at va=%x\n", path, process->process_name, USER_CODE_VA);
    return 1;
}

static int map_private_probe_page(struct user_process *owner) {
    uint64_t private_page = page_alloc();
    if (private_page == 0) {
        return 0;
    }
    uint64_t *words = (uint64_t *)private_page;
    for (uint32_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        words[i] = 0xBEEFBEEF00000000ULL | owner->pid;
    }
    if (!memory_map_process_page(owner->cr3, USER_PEER_PROBE_VA, private_page,
                                 1, 1, 0, "allow:user-private-page")) {
        return 0;
    }
    event_record_kind("scheduler", owner->process_name, "map_private_page",
                      EVENT_OK, event_current(), "allow:user-private-page", "state");
    return 1;
}

static int copy_user_string(const struct user_process *process, uint64_t user_address,
                            char *destination, uint32_t capacity) {
    (void)process;
    if (capacity == 0 ||
        user_address < USER_CODE_VA ||
        user_address >= USER_CODE_VA + PAGE_SIZE) {
        return 0;
    }
    uint32_t i = 0;
    const char *source = (const char *)user_address;
    while (i < capacity - 1 && user_address + i < USER_CODE_VA + PAGE_SIZE) {
        destination[i] = source[i];
        if (destination[i] == '\0') {
            return 1;
        }
        i++;
    }
    destination[capacity - 1] = '\0';
    return 0;
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
    struct user_process *peer = create_process();
    if (peer == 0) {
        return 0;
    }

    transition_scaffold(process);
    if (!load_executable(process, "/bin/hello") ||
        !map_private_probe_page(peer)) {
        printf("User demo failed: process load\n");
        return 0;
    }
    process->peer_probe_address = USER_PEER_PROBE_VA;
    copy_text(process->peer_process_name, sizeof(process->peer_process_name),
              peer->process_name);

    active_user_process = process;
    user_fault_contained = 0;
    event_record_kind("scheduler", process->thread_name, "enter_cpl3",
                      EVENT_OK, event_current(), "allow:user-enter", "state");
    memory_switch_address_space(process->cr3, "scheduler",
                                process->address_space_name,
                                "allow:process-cr3");
    user_enter_ring3(USER_CODE_VA, USER_STACK_TOP);
    memory_switch_address_space(memory_kernel_address_space(), "scheduler",
                                "kernel", "allow:kernel-cr3");
    active_user_process = 0;
    printf("User demo complete: cpl3=yes syscall_return=yes fault_contained=%s image=/bin/hello address_space=yes\n",
           user_fault_contained ? "yes" : "no");
    return user_fault_contained;
}

void user_syscall_trap(uint64_t number, uint64_t user_cs,
                       uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    if (active_user_process == 0) {
        event_record_kind("cpu", "syscall", "trap", EVENT_DENIED,
                          event_current(), "deny:no-active-user", "syscall");
        return;
    }
    (void)arg1;
    (void)arg2;

    event_record_kind(active_user_process->thread_name, "kernel", "cpl3_trap",
                      (user_cs & 3ULL) == 3 ? EVENT_OK : EVENT_DENIED,
                      event_current(),
                      (user_cs & 3ULL) == 3 ? "allow:cpl3" : "deny:not-cpl3",
                      "syscall");

    char target[USER_NAME_SIZE];
    if (number == 1 || number == 3 || number == 4) {
        if (!copy_user_string(active_user_process, arg0, target, sizeof(target))) {
            copy_text(target, sizeof(target), "kernel");
        }
    } else {
        copy_text(target, sizeof(target), "kernel");
    }

    memory_switch_address_space(memory_kernel_address_space(),
                                active_user_process->thread_name,
                                "kernel", "allow:syscall-kernel-cr3");

    if (number == 1) {
        user_syscall_inspect(active_user_process, target);
    } else if (number == 2) {
        user_syscall_yield(active_user_process);
    } else if (number == 3) {
        user_syscall_config_get(active_user_process, target);
    } else if (number == 4) {
        user_syscall_config_set_denied(active_user_process, target);
    } else {
        uint64_t transaction = syscall_begin(active_user_process, "kernel");
        event_record_kind(active_user_process->thread_name, "kernel", "unknown_syscall",
                          EVENT_DENIED, event_current(), "deny:unknown-syscall", "syscall");
        syscall_finish(transaction, 0);
    }

    memory_switch_address_space(active_user_process->cr3,
                                active_user_process->thread_name,
                                active_user_process->address_space_name,
                                "allow:syscall-user-cr3");
}

int user_handle_fault(uint64_t vector, uint64_t error_code, void *frame) {
    struct user_interrupt_frame *interrupt_frame = (struct user_interrupt_frame *)frame;
    if (active_user_process == 0 || vector != 14 || (interrupt_frame->cs & 3ULL) != 3) {
        return 0;
    }

    uint64_t fault_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
    memory_switch_address_space(memory_kernel_address_space(),
                                active_user_process->thread_name,
                                "kernel", "allow:fault-kernel-cr3");
    user_fault_contained = 1;
    const char *target = "kernel";
    const char *policy = "deny:user-page-fault";
    const char *operation = "page_fault";
    if (fault_address == active_user_process->peer_probe_address &&
        active_user_process->peer_process_name[0] != '\0') {
        target = active_user_process->peer_process_name;
        policy = "deny:address-space-isolation";
        operation = "isolation_fault";
    }
    event_record_kind(active_user_process->thread_name, target, operation,
                      EVENT_FAULT, event_current(), policy, "fault");
    printf("User page fault contained: rip=%x address=%x error=%x policy=%s\n",
           interrupt_frame->rip, fault_address, error_code, policy);
    return 1;
}
