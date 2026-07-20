#include "types.h"
#include "vga.h"
#include "idt.h"
#include "memory.h"
#include "timer.h"
#include "keyboard.h"
#include "io.h"
#include "panic.h"
#include "ramfs.h"
#include "config.h"
#include "user.h"
#include "serial.h"
#include "event.h"
#include "object.h"
#include "policy.h"

#define MODULE_BUFFER_SIZE 512

static char module_buffer[MODULE_BUFFER_SIZE];

// Provided by linker.ld - bounds of the .bss section
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static int starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static char *split_command(char *line) {
    while (*line != '\0' && *line != ' ') {
        line++;
    }
    if (*line == '\0') {
        return 0;
    }
    *line++ = '\0';
    while (*line == ' ') {
        line++;
    }
    return *line == '\0' ? 0 : line;
}

static uint64_t parse_sequence(const char *text) {
    uint64_t value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (uint64_t)(*text - '0');
        text++;
    }
    return *text == '\0' ? value : 0;
}

static void copy_text(char *destination, uint32_t capacity, const char *source) {
    uint32_t i = 0;
    if (source == 0) {
        source = "";
    }
    while (i < capacity - 1 && source[i] != '\0') {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static void copy_first_token(char *destination, uint32_t capacity, const char *source) {
    uint32_t i = 0;
    if (source == 0) {
        source = "";
    }
    while (i < capacity - 1 && source[i] != '\0' && source[i] != ' ') {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static void append_decimal(char *destination, uint32_t capacity, uint32_t *position, uint64_t value) {
    char digits[20];
    uint32_t count = 0;
    if (value == 0) {
        if (*position < capacity - 1) {
            destination[(*position)++] = '0';
            destination[*position] = '\0';
        }
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (count != 0 && *position < capacity - 1) {
        destination[(*position)++] = digits[--count];
    }
    destination[*position] = '\0';
}

static void build_transaction_target(char *destination, uint32_t capacity, uint64_t transaction) {
    uint32_t position = 0;
    const char *prefix = "transaction/";
    while (*prefix != '\0' && position < capacity - 1) {
        destination[position++] = *prefix++;
    }
    destination[position] = '\0';
    append_decimal(destination, capacity, &position, transaction);
}

static void show_meminfo(void) {
    struct policy_decision policy = policy_check("shell", "memory", "memory:inspect");
    if (!policy.allowed) {
        printf("Denied: %s\n", policy.reason);
        return;
    }
    event_record_kind("memory", "memory", "inspect", EVENT_OK, policy.event, policy.reason, "state");
    printf("E820 entries: %d, usable: %x KiB\n",
           (int)memory_map_entry_count(), memory_total_usable() / 1024);
    printf("Mapped: %x MiB, pages free: %d\n",
           memory_mapped_limit() / (1024 * 1024), (int)page_allocator_free_pages());
    printf("Heap: %x/%x bytes\n", heap_capacity() - heap_remaining(), heap_capacity());
}

static void show_time(void) {
    unsigned int seconds = (unsigned int)(system_ticks / 100);
    printf("System active for: %d seconds (%x ticks)\n", seconds, system_ticks);
}

static void list_files(void) {
    struct policy_decision policy = policy_check("shell", "ramfs", "filesystem:list");
    if (!policy.allowed) {
        printf("Denied: %s\n", policy.reason);
        return;
    }
    event_record_kind("ramfs", "ramfs", "list", EVENT_OK, policy.event, policy.reason, "state");
    for (uint32_t i = 0; i < ramfs_file_count(); i++) {
        const struct ramfs_file *file = ramfs_file_at(i);
        const char *label = "[file] ";
        if (file->type == RAMFS_FILE_MODULE) {
            label = "[module] ";
        } else if (file->type == RAMFS_FILE_EXECUTABLE) {
            label = "[bin] ";
        }
        printf("%s%s\n", label, file->path);
    }
}

static int show_config(const char *key) {
    struct policy_decision policy = policy_check("shell", key, "config:get");
    if (!policy.allowed) {
        printf("Denied: %s\n", policy.reason);
        return 0;
    }

    const char *value = config_get(key);
    if (value == 0) {
        event_record_kind("config", key, "get", EVENT_DENIED, policy.event,
                          "deny:unknown-config-key", "state");
        printf("Config key not found: %s\n", key);
        return 0;
    }

    event_record_kind("config", key, "get", EVENT_OK, policy.event, policy.reason, "state");
    printf("%s=%s\n", key, value);
    return 1;
}

static int set_config(char *argument) {
    char *value = split_command(argument);
    if (value == 0) {
        printf("Usage: set <key> <value>\n");
        return 0;
    }

    struct policy_decision policy = policy_check("shell", argument, "config:set");
    if (!policy.allowed) {
        printf("Denied: %s\n", policy.reason);
        return 0;
    }

    uint64_t before = object_revision(argument);
    uint64_t previous_event = event_current();
    event_set_current(policy.event);
    if (!config_set(argument, value)) {
        event_set_current(previous_event);
        printf("Config key not found: %s\n", argument);
        return 0;
    }
    event_set_current(previous_event);
    uint64_t after = object_revision(argument);
    event_record_kind("config", argument, "set", EVENT_OK, policy.event, policy.reason, "state");
    printf("%s=%s revision %d->%d\n", argument, value, (int)before, (int)after);
    return 1;
}

static int run_policy_command(char *argument) {
    if (argument == 0) {
        printf("Usage: policy show | policy allow <subject> <operation> <object> | policy deny <subject> <operation> <object>\n");
        return 0;
    }

    char *rest = split_command(argument);
    if (strcmp(argument, "show") == 0) {
        struct policy_decision policy = policy_check("shell", "policy/base.policy", "policy:show");
        if (!policy.allowed) {
            printf("Denied: %s\n", policy.reason);
            return 0;
        }
        event_record_kind("shell", "policy/base.policy", "policy_show",
                          EVENT_OK, policy.event, policy.reason, "query");
        policy_show();
        return 1;
    }

    int allow;
    if (strcmp(argument, "allow") == 0) {
        allow = 1;
    } else if (strcmp(argument, "deny") == 0) {
        allow = 0;
    } else {
        printf("Unknown policy command: %s\n", argument);
        return 0;
    }

    if (rest == 0) {
        printf("Usage: policy %s <subject> <operation> <object>\n", argument);
        return 0;
    }
    char *subject = rest;
    char *operation = split_command(subject);
    char *object = operation == 0 ? 0 : split_command(operation);
    if (operation == 0 || object == 0) {
        printf("Usage: policy %s <subject> <operation> <object>\n", argument);
        return 0;
    }

    struct policy_decision policy = policy_check("shell", "policy/base.policy", "policy:change");
    if (!policy.allowed) {
        printf("Denied: %s\n", policy.reason);
        return 0;
    }

    uint64_t previous_event = event_current();
    event_set_current(policy.event);
    int changed = policy_change_rule(allow, subject, operation, object);
    event_set_current(previous_event);
    return changed;
}

static void print_file(const char *path) {
    struct policy_decision policy = policy_check("shell", path, "filesystem:read");
    if (!policy.allowed) {
        printf("Denied: %s\n", policy.reason);
        return;
    }
    uint64_t command_event = event_current();
    event_set_current(policy.event);
    const struct ramfs_file *file = ramfs_lookup(path);
    event_set_current(command_event);
    if (file == 0) {
        printf("File not found: %s\n", path);
        return;
    }

    event_record_kind("ramfs", path, "read", EVENT_OK, policy.event, policy.reason, "state");
    for (uint64_t i = 0; i < file->size; i++) {
        print_char((char)file->data[i]);
    }
    if (file->size == 0 || file->data[file->size - 1] != '\n') {
        print_char('\n');
    }
}

static void execute_module_line(char *line) {
    if (*line == '\0' || *line == '#') {
        return;
    }
    if (starts_with(line, "print ")) {
        printf("%s\n", line + 6);
    } else if (strcmp(line, "meminfo") == 0) {
        show_meminfo();
    } else if (strcmp(line, "time") == 0) {
        show_time();
    } else if (strcmp(line, "clear") == 0) {
        vga_clear_window_content();
    } else if (strcmp(line, "window") == 0) {
        vga_draw_window();
    } else {
        printf("Module directive not supported: %s\n", line);
    }
}

static void run_module(const char *path) {
    struct policy_decision policy = policy_check("shell", path, "module:load");
    if (!policy.allowed) {
        printf("Denied: %s\n", policy.reason);
        return;
    }
    uint64_t command_event = event_current();
    event_set_current(policy.event);
    const struct ramfs_file *file = ramfs_lookup(path);
    event_set_current(command_event);
    if (file == 0) {
        printf("Module not found: %s\n", path);
        return;
    }
    if (file->type != RAMFS_FILE_MODULE) {
        event_record_kind("ramfs", path, "type_check", EVENT_DENIED, policy.event, "deny:not-module", "policy");
        printf("Not a module: %s\n", path);
        return;
    }
    if (file->size >= MODULE_BUFFER_SIZE) {
        event_record_kind("ramfs", path, "size_check", EVENT_DENIED, policy.event, "deny:too-large", "policy");
        printf("Module too large: %s\n", path);
        return;
    }

    uint64_t bytes = ramfs_read(file, 0, module_buffer, MODULE_BUFFER_SIZE - 1);
    module_buffer[bytes] = '\0';
    event_record_kind("ramfs", path, "read", EVENT_OK, policy.event, policy.reason, "state");
    printf("Loading %s\n", path);

    char *line = module_buffer;
    while (*line != '\0') {
        char *next = line;
        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next++ = '\0';
        }
        execute_module_line(line);
        line = next;
    }
}

// objcopy -O binary does not write zero bytes for .bss - it only tracks
// the size. Since stage2 copies the raw file bytes from disk into RAM,
// every .bss variable (kbd_buffer, buffer_idx, command_ready, idt[256],
// system_ticks, ...) starts as whatever was already sitting in that RAM,
// not zero. Zero it explicitly before anything else runs.
void zero_bss(void) {
    for (uint8_t *p = __bss_start; p < __bss_end; p++) {
        *p = 0;
    }
}

void kernel_main(uint64_t multiboot_info_address) {
    serial_write("GlassOS: entered kernel_main\r\n");
    event_init();
    uint64_t boot_event = event_record("boot", "kernel", "enter", EVENT_OK, 0, "allow:boot");
    event_set_current(boot_event);
    object_registry_init(boot_event);
    policy_init(boot_event);
    clear_screen();
    vga_enable_cursor();

    while (inb(0x64) & 1) {
        inb(0x60);
    }

    serial_write("GlassOS: initializing memory\r\n");
    ASSERT(memory_init((uint32_t)multiboot_info_address));
    object_update_state("memory", "ready");
    serial_write("GlassOS: memory ready\r\n");
    ASSERT(ramfs_init());
    object_update_state("ramfs", "ready");
    serial_write("GlassOS: RAM filesystem ready\r\n");
    ASSERT(config_init());
    serial_write("GlassOS: config ready\r\n");
    user_init();
    serial_write("GlassOS: user model ready\r\n");
    init_idt();
    object_update_state("interrupts", "ready");
    serial_write("GlassOS: IDT ready\r\n");

    event_record("boot", "interrupts", "initialize", EVENT_OK, 1, "allow:kernel");
    serial_write("GlassOS serial: kernel ready\r\n");
    vga_draw_window();
    object_update_state("shell", "ready");
    printf("GlassOS Shell v2.0 - RAM filesystem ready\n");

    while (1) {
        __asm__ volatile ("cli");
        keyboard_begin_line();
        printf("> ");
        __asm__ volatile ("sti");

        while (!command_ready) {
            __asm__ volatile ("hlt");
        }

        char *argument = split_command(kbd_buffer);
        char transaction_target[48];
        if (strcmp(kbd_buffer, "set") == 0 && argument != 0) {
            copy_first_token(transaction_target, sizeof(transaction_target), argument);
        } else if (strcmp(kbd_buffer, "policy") == 0) {
            copy_text(transaction_target, sizeof(transaction_target), "policy/base.policy");
        } else if (strcmp(kbd_buffer, "userdemo") == 0) {
            copy_text(transaction_target, sizeof(transaction_target), "scheduler");
        } else if (strcmp(kbd_buffer, "why") == 0 && argument != 0 && starts_with(argument, "tx ")) {
            build_transaction_target(transaction_target, sizeof(transaction_target), parse_sequence(argument + 3));
        } else {
            copy_text(transaction_target, sizeof(transaction_target), argument == 0 ? kbd_buffer : argument);
        }
        uint64_t command_event = event_record_kind("shell", transaction_target, "command", EVENT_OK, event_current(), kbd_buffer, "command");
        uint64_t transaction = transaction_begin("shell", transaction_target, command_event);
        int transaction_ok = transaction != 0;
        event_set_current(transaction == 0 ? command_event : transaction);
        event_set_transaction(transaction);
        if (strcmp(kbd_buffer, "help") == 0) {
            printf("Commands: help, clear, window, meminfo, time, ls, cat <path>, run <module>, get <key>, set <key> <value>, policy <op>, userdemo, events\n");
            printf("Truth: inspect <object>, expand <object>, why <event>, history <object>\n");
        }
        else if (strcmp(kbd_buffer, "clear") == 0) {
            struct policy_decision policy = policy_check("shell", "window", "ui:clear");
            if (policy.allowed) {
                vga_clear_window_content();
                event_record_kind("shell", "window", "clear", EVENT_OK, policy.event, policy.reason, "state");
            } else {
                transaction_ok = 0;
                printf("Denied: %s\n", policy.reason);
            }
        }
        else if (strcmp(kbd_buffer, "window") == 0) {
            struct policy_decision policy = policy_check("shell", "window", "ui:redraw");
            if (policy.allowed) {
                vga_draw_window();
                event_record_kind("shell", "window", "redraw", EVENT_OK, policy.event, policy.reason, "state");
                printf("Window restored. All shell functionality is available.\n");
            } else {
                transaction_ok = 0;
                printf("Denied: %s\n", policy.reason);
            }
        }
        else if (strcmp(kbd_buffer, "meminfo") == 0) {
            show_meminfo();
        }
        else if (strcmp(kbd_buffer, "time") == 0) {
            show_time();
        }
        else if (strcmp(kbd_buffer, "ls") == 0) {
            list_files();
        }
        else if (strcmp(kbd_buffer, "cat") == 0 && argument != 0) {
            print_file(argument);
        }
        else if (strcmp(kbd_buffer, "run") == 0 && argument != 0) {
            run_module(argument);
        }
        else if (strcmp(kbd_buffer, "get") == 0 && argument != 0) {
            if (!show_config(argument)) {
                transaction_ok = 0;
            }
        }
        else if (strcmp(kbd_buffer, "set") == 0 && argument != 0) {
            if (!set_config(argument)) {
                transaction_ok = 0;
            }
        }
        else if (strcmp(kbd_buffer, "policy") == 0) {
            if (!run_policy_command(argument)) {
                transaction_ok = 0;
            }
        }
        else if (strcmp(kbd_buffer, "userdemo") == 0) {
            if (!user_run_demo()) {
                transaction_ok = 0;
            }
        }
        else if (strcmp(kbd_buffer, "events") == 0) {
            printf("Event buffer: %d retained\n", (int)event_count());
        }
        else if (strcmp(kbd_buffer, "inspect") == 0 && argument != 0) {
            struct policy_decision policy = policy_check("shell", argument, "inspect");
            if (policy.allowed) {
                event_record_kind("shell", argument, "inspect", EVENT_OK, policy.event, policy.reason, "query");
                object_inspect(argument);
            } else {
                transaction_ok = 0;
                printf("Denied: %s\n", policy.reason);
            }
        }
        else if (strcmp(kbd_buffer, "expand") == 0 && argument != 0) {
            struct policy_decision policy = policy_check("shell", argument, "expand");
            if (policy.allowed) {
                event_record_kind("shell", argument, "expand", EVENT_OK, policy.event, policy.reason, "query");
                object_expand(argument);
            } else {
                transaction_ok = 0;
                printf("Denied: %s\n", policy.reason);
            }
        }
        else if (strcmp(kbd_buffer, "why") == 0 && argument != 0) {
            uint64_t sequence = parse_sequence(argument);
            const char *why_target = starts_with(argument, "tx ") ? transaction_target : argument;
            struct policy_decision policy = policy_check("shell", why_target, "why");
            if (policy.allowed) {
                event_record_kind("shell", why_target, "why", EVENT_OK, policy.event, policy.reason, "query");
                if (starts_with(argument, "tx ")) {
                    event_print_transaction(parse_sequence(argument + 3));
                } else if (sequence == 0) {
                    object_history(argument);
                } else {
                    event_print_chain(sequence);
                }
            } else {
                transaction_ok = 0;
                printf("Denied: %s\n", policy.reason);
            }
        }
        else if (strcmp(kbd_buffer, "history") == 0 && argument != 0) {
            struct policy_decision policy = policy_check("shell", argument, "history");
            if (policy.allowed) {
                event_record_kind("shell", argument, "history", EVENT_OK, policy.event, policy.reason, "query");
                object_history(argument);
            } else {
                transaction_ok = 0;
                printf("Denied: %s\n", policy.reason);
            }
        }
#ifdef GLASS_TEST_FAULT_INJECTION
        else if (strcmp(kbd_buffer, "fault") == 0) {
            event_record("shell", "kernel", "fault_injection", EVENT_FAULT, command_event, "allow:test-fault");
            __asm__ volatile ("ud2");
        }
#endif
#ifdef GLASS_TEST_POLICY_DENY
        else if (strcmp(kbd_buffer, "deny-policy") == 0) {
            struct policy_decision policy = policy_check("shell", "kernel", "kernel:reboot");
            if (!policy.allowed) {
                transaction_ok = 0;
                printf("Denied: %s\n", policy.reason);
            } else {
                event_record_kind("shell", "kernel", "reboot", EVENT_OK, policy.event, policy.reason, "state");
            }
        }
#endif
        else {
            event_record("shell", kbd_buffer, "command", EVENT_DENIED, command_event, "deny:unknown-command");
            transaction_ok = 0;
            printf("Unknown command: %s\n", kbd_buffer);
        }

        if (transaction != 0) {
            if (transaction_ok) {
                transaction_commit(transaction);
            } else {
                transaction_abort(transaction, "deny:transaction");
            }
        }
        event_set_transaction(0);
        event_set_current(boot_event);
    }
}
