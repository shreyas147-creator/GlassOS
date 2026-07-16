#ifndef POLICY_H
#define POLICY_H

#include "types.h"

#define POLICY_DECISION_SIZE 48

struct policy_decision {
    int allowed;
    uint64_t event;
    char reason[POLICY_DECISION_SIZE];
};

void policy_init(uint64_t boot_event);
uint32_t policy_revision(void);
void policy_show(void);
int policy_change_rule(int allow, const char *subject, const char *operation, const char *object);
void policy_print_capabilities_for_object(const char *object_name);
struct policy_decision policy_check(const char *actor, const char *target, const char *operation);

#endif
