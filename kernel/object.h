#ifndef OBJECT_H
#define OBJECT_H

#include "types.h"

struct kernel_object {
    uint32_t id;
    uint32_t parent_id;
    uint64_t revision;
    uint64_t created_event;
    uint64_t last_event;
    char type[32];
    char name[48];
    char state[48];
};

void object_registry_init(uint64_t boot_event);
uint32_t object_register(const char *type, const char *name, uint32_t parent_id, const char *state, uint64_t created_event);
void object_update_state(const char *name, const char *state);
void object_set_state_direct(const char *name, const char *state, uint64_t event);
const struct kernel_object *object_find(const char *query);
uint32_t object_id(const char *query);
uint64_t object_revision(const char *query);
void object_inspect(const char *query);
void object_expand(const char *query);
void object_history(const char *query);

#endif
