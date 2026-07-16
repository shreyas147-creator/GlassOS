#ifndef EVENT_H
#define EVENT_H
#include "types.h"
enum event_result { EVENT_OK, EVENT_DENIED, EVENT_FAULT };
#define EVENT_LABEL_SIZE 48
struct system_event {
    uint64_t sequence;
    uint64_t timestamp;
    uint64_t parent_event;
    uint64_t transaction_id;
    uint32_t actor_object_id;
    uint32_t target_object_id;
    uint64_t target_revision;
    char kind[24];
    char actor[EVENT_LABEL_SIZE];
    char target[EVENT_LABEL_SIZE];
    char operation[EVENT_LABEL_SIZE];
    char policy_decision[EVENT_LABEL_SIZE];
    enum event_result result;
};
void event_init(void);
uint64_t event_record(const char *actor, const char *target, const char *operation, enum event_result result, uint64_t parent, const char *policy);
uint64_t event_record_kind(const char *actor, const char *target, const char *operation, enum event_result result, uint64_t parent, const char *policy, const char *kind);
void event_set_current(uint64_t sequence);
uint64_t event_current(void);
void event_set_transaction(uint64_t sequence);
uint64_t event_current_transaction(void);
uint64_t transaction_begin(const char *actor, const char *target, uint64_t parent_event);
uint64_t transaction_commit(uint64_t transaction_id);
uint64_t transaction_abort(uint64_t transaction_id, const char *reason);
uint32_t event_count(void);
const struct system_event *event_at(uint32_t index);
const struct system_event *event_find(uint64_t sequence);
void event_print(const struct system_event *event);
void event_print_chain(uint64_t sequence);
void event_print_history(const char *target);
void event_print_transaction(uint64_t transaction_id);
void event_print_transaction_name(const char *query);
#endif
