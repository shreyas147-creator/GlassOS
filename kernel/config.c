#include "config.h"
#include "event.h"
#include "object.h"

#define CONFIG_VALUE_SIZE 48

struct config_entry {
    const char *key;
    char value[CONFIG_VALUE_SIZE];
};

static struct config_entry entries[] = {
    { "/config/theme", "light" },
    { "/config/prompt", ">" }
};

static void copy_value(char *destination, uint32_t capacity, const char *source) {
    uint32_t i = 0;
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

static struct config_entry *find_entry(const char *key) {
    for (uint32_t i = 0; i < (uint32_t)(sizeof(entries) / sizeof(entries[0])); i++) {
        if (text_equal(entries[i].key, key)) {
            return &entries[i];
        }
    }
    return 0;
}

int config_init(void) {
    uint32_t parent = object_register("directory", "/config", object_id("system"),
                                      "ready", event_current());
    for (uint32_t i = 0; i < (uint32_t)(sizeof(entries) / sizeof(entries[0])); i++) {
        object_register("config_key", entries[i].key, parent,
                        entries[i].value, event_current());
    }
    event_record("config", "/config", "initialize", EVENT_OK, event_current(), "allow:boot-config");
    return 1;
}

const char *config_get(const char *key) {
    struct config_entry *entry = find_entry(key);
    return entry == 0 ? 0 : entry->value;
}

int config_set(const char *key, const char *value) {
    struct config_entry *entry = find_entry(key);
    if (entry == 0) {
        event_record_kind("config", key, "set", EVENT_DENIED, event_current(),
                          "deny:unknown-config-key", "state");
        return 0;
    }

    copy_value(entry->value, sizeof(entry->value), value);
    object_update_state(key, entry->value);
    return 1;
}
