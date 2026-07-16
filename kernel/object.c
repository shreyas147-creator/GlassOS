#include "object.h"
#include "event.h"
#include "policy.h"
#include "vga.h"

#define OBJECT_CAPACITY 128

static struct kernel_object objects[OBJECT_CAPACITY];
static uint32_t object_count;
static uint32_t next_id;

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

static int text_equal(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static uint32_t parse_id(const char *text) {
    uint32_t value = 0;
    uint32_t digits = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (uint32_t)(*text - '0');
        text++;
        digits++;
    }
    return (*text == '\0' && digits > 0) ? value : 0;
}

void object_registry_init(uint64_t boot_event) {
    object_count = 0;
    next_id = 1;
    uint32_t system = object_register("system", "system", 0, "booting", boot_event);
    object_register("boot", "boot", system, "entered kernel", boot_event);
    object_register("subsystem", "memory", system, "initializing", boot_event);
    object_register("subsystem", "interrupts", system, "pending", boot_event);
    object_register("subsystem", "ramfs", system, "pending", boot_event);
    object_register("subsystem", "shell", system, "pending", boot_event);
    object_register("ledger", "events", system, "recording", boot_event);
}

uint32_t object_register(const char *type, const char *name, uint32_t parent_id, const char *state, uint64_t created_event) {
    if (object_count >= OBJECT_CAPACITY) {
        return 0;
    }
    const struct kernel_object *existing = object_find(name);
    if (existing != 0) {
        return existing->id;
    }

    struct kernel_object *object = &objects[object_count++];
    object->id = next_id++;
    object->parent_id = parent_id;
    object->revision = 1;
    object->created_event = created_event;
    object->last_event = created_event;
    copy_text(object->type, sizeof(object->type), type);
    copy_text(object->name, sizeof(object->name), name);
    copy_text(object->state, sizeof(object->state), state);
    return object->id;
}

void object_update_state(const char *name, const char *state) {
    struct kernel_object *object = (struct kernel_object *)object_find(name);
    if (object != 0) {
        copy_text(object->state, sizeof(object->state), state);
        object->revision++;
        object->last_event = event_record_kind("object", object->name, "state_update",
                                               EVENT_OK, event_current(), "allow:object-state", "state");
    }
}

void object_set_state_direct(const char *name, const char *state, uint64_t event) {
    struct kernel_object *object = (struct kernel_object *)object_find(name);
    if (object != 0) {
        copy_text(object->state, sizeof(object->state), state);
        object->revision++;
        object->last_event = event;
    }
}

uint32_t object_id(const char *query) {
    const struct kernel_object *object = object_find(query);
    return object == 0 ? 0 : object->id;
}

uint64_t object_revision(const char *query) {
    const struct kernel_object *object = object_find(query);
    return object == 0 ? 0 : object->revision;
}

const struct kernel_object *object_find(const char *query) {
    uint32_t id = parse_id(query);
    for (uint32_t i = 0; i < object_count; i++) {
        if ((id != 0 && objects[i].id == id) || text_equal(objects[i].name, query)) {
            return &objects[i];
        }
    }
    return 0;
}

void object_inspect(const char *query) {
    const struct kernel_object *object = object_find(query);
    if (object == 0) {
        printf("Object not found: %s\n", query);
        return;
    }
    printf("object #%d %s %s\n", (int)object->id, object->type, object->name);
    printf("state=%s revision=%d parent=%d created_event=%d last_event=%d\n",
           object->state, (int)object->revision,
           (int)object->parent_id, (int)object->created_event, (int)object->last_event);
    if (text_equal(object->type, "policy")) {
        policy_print_capabilities_for_object(object->name);
    } else if (text_equal(object->type, "transaction")) {
        event_print_transaction_name(object->name);
    }
}

static void print_object_summary(const struct kernel_object *object) {
    printf("  #%d %s %s state=%s revision=%d\n",
           (int)object->id, object->type, object->name,
           object->state, (int)object->revision);
}

static void print_parent_expansion(const struct kernel_object *object) {
    if (object->parent_id == 0) {
        return;
    }
    for (uint32_t i = 0; i < object_count; i++) {
        if (objects[i].id == object->parent_id) {
            printf("  parent -> expand %s requires policy expand\n", objects[i].name);
            return;
        }
    }
}

static void print_next_expansions(const struct kernel_object *object) {
    printf("available next expansions:\n");
    printf("  self -> inspect %s requires policy inspect\n", object->name);
    print_parent_expansion(object);
    uint32_t children = 0;
    for (uint32_t i = 0; i < object_count; i++) {
        if (objects[i].parent_id == object->id) {
            printf("  child -> expand %s requires policy expand\n", objects[i].name);
            children++;
        }
    }
    if (children == 0) {
        printf("  children -> none\n");
    }
    printf("  history -> history %s requires policy history\n", object->name);
    printf("  cause -> why %d requires policy why\n", (int)object->last_event);
    printf("  policy -> inspect policy/base.policy requires policy inspect\n");
}

static void print_available_mutations(const struct kernel_object *object) {
    printf("available mutations:\n");
    if (text_equal(object->type, "file")) {
        printf("  cat %s requires policy filesystem:read\n", object->name);
    } else if (text_equal(object->type, "module")) {
        printf("  run %s requires policy module:load\n", object->name);
    } else if (text_equal(object->name, "ramfs")) {
        printf("  ls requires policy filesystem:list\n");
    } else if (text_equal(object->name, "memory")) {
        printf("  meminfo requires policy memory:inspect\n");
    } else if (text_equal(object->type, "config_key")) {
        printf("  get %s requires policy config:get\n", object->name);
        printf("  set %s <value> requires policy config:set\n", object->name);
    } else {
        printf("  none\n");
    }
    printf("policy revision=%d\n", (int)policy_revision());
}

void object_expand(const char *query) {
    const struct kernel_object *object = object_find(query);
    if (object == 0) {
        printf("Object not found: %s\n", query);
        return;
    }

    object_inspect(query);
    if (object->parent_id != 0) {
        printf("parent:\n");
        for (uint32_t i = 0; i < object_count; i++) {
            if (objects[i].id == object->parent_id) {
                print_object_summary(&objects[i]);
                break;
            }
        }
    }

    printf("children:\n");
    uint32_t children = 0;
    for (uint32_t i = 0; i < object_count; i++) {
        if (objects[i].parent_id == object->id) {
            print_object_summary(&objects[i]);
            children++;
        }
    }
    if (children == 0) {
        printf("  none\n");
    }

    print_next_expansions(object);
    print_available_mutations(object);
    policy_print_capabilities_for_object(object->name);

    printf("causal history:\n");
    object_history(object->name);
    for (uint32_t i = 0; i < object_count; i++) {
        if (objects[i].parent_id == object->id) {
            printf("child history %s:\n", objects[i].name);
            event_print_history(objects[i].name);
        }
    }
}

void object_history(const char *query) {
    const struct kernel_object *object = object_find(query);
    if (object == 0) {
        event_print_history(query);
        return;
    }
    event_print_history(object->name);
}
