#include "policy.h"
#include "event.h"
#include "object.h"
#include "vga.h"

#define POLICY_MAX_RULES 64
#define POLICY_FIELD_SIZE 48

static const char base_policy_source[] =
#include "base_policy.inc"
;

struct policy_rule {
    int allow;
    uint32_t rule_number;
    char capability[POLICY_FIELD_SIZE];
    char subject[POLICY_FIELD_SIZE];
    char operation[POLICY_FIELD_SIZE];
    char object[POLICY_FIELD_SIZE];
};

static struct policy_rule rules[POLICY_MAX_RULES];
static uint32_t rule_count;
static uint32_t active_revision;
static int policy_loaded;

static int text_equal(const char *left, const char *right) {
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int starts_with(const char *text, const char *prefix) {
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static int field_matches(const char *rule_field, const char *value) {
    return text_equal(rule_field, "*") || text_equal(rule_field, value);
}

static void copy_field(char *destination, uint32_t capacity, const char *source) {
    uint32_t i = 0;
    while (i < capacity - 1 && source[i] != '\0') {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static const char *skip_spaces(const char *text) {
    while (*text == ' ' || *text == '\t') {
        text++;
    }
    return text;
}

static const char *read_token(const char *text, char *destination, uint32_t capacity) {
    uint32_t i = 0;
    text = skip_spaces(text);
    while (*text != '\0' && *text != ' ' && *text != '\t' &&
           *text != '\r' && *text != '\n') {
        if (i < capacity - 1) {
            destination[i++] = *text;
        }
        text++;
    }
    destination[i] = '\0';
    return text;
}

static uint32_t parse_decimal(const char *text) {
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (uint32_t)(*text - '0');
        text++;
    }
    return value;
}

static void parse_revision_comment(const char *line) {
    const char *prefix = "# GlassOS policy revision ";
    if (starts_with(line, prefix)) {
        active_revision = parse_decimal(line + 26);
    }
}

static void parse_rule_line(const char *line) {
    char effect[8];
    char subject[POLICY_FIELD_SIZE];
    char operation[POLICY_FIELD_SIZE];
    char object[POLICY_FIELD_SIZE];

    line = read_token(line, effect, sizeof(effect));
    line = read_token(line, subject, sizeof(subject));
    line = read_token(line, operation, sizeof(operation));
    read_token(line, object, sizeof(object));

    if ((!text_equal(effect, "allow") && !text_equal(effect, "deny")) ||
        subject[0] == '\0' || operation[0] == '\0' || object[0] == '\0' ||
        rule_count >= POLICY_MAX_RULES) {
        return;
    }

    struct policy_rule *rule = &rules[rule_count];
    rule->allow = text_equal(effect, "allow");
    rule->rule_number = rule_count + 1;
    rule->capability[0] = '\0';
    copy_field(rule->subject, sizeof(rule->subject), subject);
    copy_field(rule->operation, sizeof(rule->operation), operation);
    copy_field(rule->object, sizeof(rule->object), object);
    rule_count++;
}

static void policy_parse(const char *source) {
    char line[160];
    rule_count = 0;
    active_revision = 0;

    while (*source != '\0') {
        uint32_t length = 0;
        while (source[length] != '\0' && source[length] != '\n' &&
               length < sizeof(line) - 1) {
            line[length] = source[length];
            length++;
        }
        line[length] = '\0';
        while (*source != '\0' && *source != '\n') {
            source++;
        }
        if (*source == '\n') {
            source++;
        }

        const char *trimmed = skip_spaces(line);
        if (*trimmed == '\0') {
            continue;
        }
        if (*trimmed == '#') {
            parse_revision_comment(trimmed);
            continue;
        }
        parse_rule_line(trimmed);
    }

    if (active_revision == 0) {
        active_revision = 1;
    }
    policy_loaded = 1;
}

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

static void build_capability_name(char *destination, uint32_t capacity,
                                  const char *subject, const char *operation,
                                  const char *object) {
    uint32_t position = 0;
    append_text(destination, capacity, &position, "capability/");
    append_text(destination, capacity, &position, subject);
    append_text(destination, capacity, &position, "/");
    append_text(destination, capacity, &position, operation);
    append_text(destination, capacity, &position, "/");
    append_text(destination, capacity, &position, object);
}

static void build_capability_state(char *destination, uint32_t capacity,
                                   int allow, uint32_t rule_number) {
    uint32_t position = 0;
    append_text(destination, capacity, &position, allow ? "allow rule " : "deny rule ");
    append_decimal(destination, capacity, &position, rule_number);
    append_text(destination, capacity, &position, " rev ");
    append_decimal(destination, capacity, &position, active_revision);
}

static void format_decision(char *destination, int allowed, const char *operation, uint32_t rule_number) {
    uint32_t position = 0;
    append_text(destination, POLICY_DECISION_SIZE, &position, allowed ? "allow:" : "deny:");
    append_text(destination, POLICY_DECISION_SIZE, &position, operation);
    append_text(destination, POLICY_DECISION_SIZE, &position, ",rev=");
    append_decimal(destination, POLICY_DECISION_SIZE, &position, active_revision);
    append_text(destination, POLICY_DECISION_SIZE, &position, ",rule=");
    append_decimal(destination, POLICY_DECISION_SIZE, &position, rule_number);
}

static void register_capability_object(struct policy_rule *rule, uint32_t policy_object) {
    char capability_state[POLICY_FIELD_SIZE];
    build_capability_name(rule->capability, sizeof(rule->capability),
                          rule->subject, rule->operation, rule->object);
    build_capability_state(capability_state, sizeof(capability_state),
                           rule->allow, rule->rule_number);
    object_register("capability", rule->capability, policy_object,
                    capability_state, event_current());
}

void policy_init(uint64_t boot_event) {
    char bootstrap_decision[POLICY_DECISION_SIZE];
    uint32_t policy_object = object_register("policy", "policy/base.policy", object_id("system"), "loading", boot_event);
    uint64_t transaction = transaction_begin("policy", "policy/base.policy", boot_event);
    event_set_current(transaction);
    event_set_transaction(transaction);

    policy_parse(base_policy_source);
    format_decision(bootstrap_decision, 1, "policy-bootstrap", 0);
    for (uint32_t i = 0; i < rule_count; i++) {
        register_capability_object(&rules[i], policy_object);
    }
    event_record_kind("policy", "policy/base.policy", "load", EVENT_OK, boot_event,
                      bootstrap_decision, "state");
    object_update_state("policy/base.policy", "loaded");
    transaction_commit(transaction);
    event_set_transaction(0);
    event_set_current(boot_event);
}

uint32_t policy_revision(void) {
    if (!policy_loaded) {
        policy_parse(base_policy_source);
    }
    return active_revision;
}

void policy_show(void) {
    if (!policy_loaded) {
        policy_parse(base_policy_source);
    }
    printf("policy/base.policy revision=%d rules=%d\n", (int)active_revision, (int)rule_count);
    for (uint32_t i = 0; i < rule_count; i++) {
        const struct policy_rule *rule = &rules[i];
        printf("  rule=%d %s %s %s %s capability=%s\n",
               (int)rule->rule_number,
               rule->allow ? "allow" : "deny",
               rule->subject,
               rule->operation,
               rule->object,
               rule->capability);
    }
}

int policy_change_rule(int allow, const char *subject, const char *operation, const char *object) {
    if (!policy_loaded) {
        policy_parse(base_policy_source);
    }
    if (rule_count >= POLICY_MAX_RULES) {
        event_record_kind("policy", "policy/base.policy", allow ? "allow" : "deny",
                          EVENT_DENIED, event_current(), "deny:policy-full", "state");
        return 0;
    }

    active_revision++;
    struct policy_rule *rule = &rules[rule_count];
    rule->allow = allow;
    rule->rule_number = rule_count + 1;
    copy_field(rule->subject, sizeof(rule->subject), subject);
    copy_field(rule->operation, sizeof(rule->operation), operation);
    copy_field(rule->object, sizeof(rule->object), object);
    rule_count++;
    register_capability_object(rule, object_id("policy/base.policy"));

    char decision[POLICY_DECISION_SIZE];
    format_decision(decision, 1, allow ? "policy:allow" : "policy:deny", rule->rule_number);
    event_record_kind("policy", "policy/base.policy", allow ? "allow" : "deny",
                      EVENT_OK, event_current(), decision, "state");
    object_update_state("policy/base.policy", allow ? "allow-added" : "deny-added");
    printf("policy revision=%d rule=%d %s %s %s %s\n",
           (int)active_revision,
           (int)rule->rule_number,
           allow ? "allow" : "deny",
           subject,
           operation,
           object);
    return 1;
}

static int rule_relevant_to_object(const struct policy_rule *rule, const char *object_name) {
    return text_equal(object_name, "policy/base.policy") ||
           text_equal(rule->subject, object_name) ||
           text_equal(rule->object, object_name) ||
           text_equal(rule->object, "*");
}

void policy_print_capabilities_for_object(const char *object_name) {
    uint32_t shown = 0;
    if (!policy_loaded) {
        policy_parse(base_policy_source);
    }

    printf("relevant capabilities:\n");
    for (uint32_t i = 0; i < rule_count; i++) {
        const struct policy_rule *rule = &rules[i];
        if (rule_relevant_to_object(rule, object_name)) {
            printf("  %s %s %s %s rule=%d rev=%d\n",
                   rule->capability,
                   rule->allow ? "allows" : "denies",
                   rule->subject,
                   rule->operation,
                   (int)rule->rule_number,
                   (int)active_revision);
            shown++;
        }
    }
    if (shown == 0) {
        printf("  none\n");
    }
}

struct policy_decision policy_check(const char *actor, const char *target, const char *operation) {
    struct policy_decision decision;
    decision.allowed = 0;
    if (!policy_loaded) {
        policy_parse(base_policy_source);
    }
    format_decision(decision.reason, 0, "policy-default", 0);

    for (uint32_t i = 0; i < rule_count; i++) {
        const struct policy_rule *rule = &rules[i];
        if (field_matches(rule->subject, actor) &&
            field_matches(rule->operation, operation) &&
            field_matches(rule->object, target)) {
            decision.allowed = rule->allow;
            format_decision(decision.reason, decision.allowed, operation, rule->rule_number);
        }
    }

    decision.event = event_record_kind(actor, target, "policy_check",
                                       decision.allowed ? EVENT_OK : EVENT_DENIED,
                                       event_current(), decision.reason, "policy");
    return decision;
}
