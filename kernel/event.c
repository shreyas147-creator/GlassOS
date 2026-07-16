#include "event.h"
#include "serial.h"
#include "timer.h"
#include "vga.h"
#include "object.h"
#define EVENT_CAPACITY 128
#define TRANSACTION_CAPACITY 32
static struct system_event entries[EVENT_CAPACITY];
static uint64_t next_sequence;
static uint64_t current_sequence;
static uint64_t current_transaction;
static uint32_t first, count;

struct transaction_record {
    uint64_t id;
    uint64_t parent_event;
    uint64_t begin_event;
    uint64_t end_event;
    uint64_t started_revision;
    uint64_t committed_revision;
    char actor[EVENT_LABEL_SIZE];
    char target[EVENT_LABEL_SIZE];
    char status[16];
};

static struct transaction_record transactions[TRANSACTION_CAPACITY];
static uint32_t transaction_count;

static void copy_field(char *destination, uint32_t capacity, const char *source) {
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

static const char *result_name(enum event_result result) {
    if (result == EVENT_OK) {
        return "ok";
    }
    if (result == EVENT_DENIED) {
        return "denied";
    }
    return "fault";
}

static struct transaction_record *transaction_find(uint64_t transaction_id) {
    for (uint32_t i = 0; i < transaction_count; i++) {
        if (transactions[i].id == transaction_id) {
            return &transactions[i];
        }
    }
    return 0;
}

static uint64_t parse_decimal64(const char *text) {
    uint64_t value = 0;
    uint32_t digits = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (uint64_t)(*text - '0');
        text++;
        digits++;
    }
    return (*text == '\0' && digits > 0) ? value : 0;
}

static void append_text(char *destination, uint32_t capacity, uint32_t *position, const char *text) {
    while (*text != '\0' && *position < capacity - 1) {
        destination[*position] = *text;
        (*position)++;
        text++;
    }
    destination[*position] = '\0';
}

static void append_decimal(char *destination, uint32_t capacity, uint32_t *position, uint64_t value) {
    char digits[20];
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

static void transaction_name(char *destination, uint32_t capacity, uint64_t transaction_id) {
    uint32_t position = 0;
    append_text(destination, capacity, &position, "transaction/");
    append_decimal(destination, capacity, &position, transaction_id);
}

void event_init(void) {
    next_sequence = 1;
    current_sequence = 0;
    current_transaction = 0;
    first = count = 0;
    transaction_count = 0;
}

uint64_t event_record(const char *actor, const char *target, const char *operation, enum event_result result, uint64_t parent, const char *policy) {
    return event_record_kind(actor, target, operation, result, parent, policy, "action");
}

uint64_t event_record_kind(const char *actor, const char *target, const char *operation, enum event_result result, uint64_t parent, const char *policy, const char *kind) {
    uint32_t slot = (first + count) % EVENT_CAPACITY;
    if (count == EVENT_CAPACITY) { slot = first; first = (first + 1) % EVENT_CAPACITY; } else count++;
    entries[slot].sequence = next_sequence++;
    entries[slot].timestamp = system_ticks;
    entries[slot].parent_event = parent;
    entries[slot].transaction_id = current_transaction;
    entries[slot].actor_object_id = object_id(actor);
    entries[slot].target_object_id = object_id(target);
    entries[slot].target_revision = object_revision(target);
    entries[slot].result = result;
    copy_field(entries[slot].kind, sizeof(entries[slot].kind), kind);
    copy_field(entries[slot].actor, sizeof(entries[slot].actor), actor);
    copy_field(entries[slot].target, sizeof(entries[slot].target), target);
    copy_field(entries[slot].operation, sizeof(entries[slot].operation), operation);
    copy_field(entries[slot].policy_decision, sizeof(entries[slot].policy_decision), policy);
    struct transaction_record *transaction = transaction_find(entries[slot].transaction_id);
    if (transaction != 0) {
        transaction->end_event = entries[slot].sequence;
    }
    serial_write("event seq="); serial_write_decimal(entries[slot].sequence); serial_write(" tx="); serial_write_decimal(entries[slot].transaction_id); serial_write(" kind="); serial_write(entries[slot].kind); serial_write(" actor#="); serial_write_decimal(entries[slot].actor_object_id); serial_write(" target#="); serial_write_decimal(entries[slot].target_object_id); serial_write(" target_rev="); serial_write_decimal(entries[slot].target_revision); serial_write(" parent="); serial_write_decimal(entries[slot].parent_event); serial_write(" actor="); serial_write(entries[slot].actor); serial_write(" target="); serial_write(entries[slot].target); serial_write(" op="); serial_write(entries[slot].operation); serial_write(" result="); serial_write(result_name(result)); serial_write(" policy="); serial_write(entries[slot].policy_decision); serial_write("\r\n");
    return entries[slot].sequence;
}

void event_set_current(uint64_t sequence) { current_sequence = sequence; }
uint64_t event_current(void) { return current_sequence; }
void event_set_transaction(uint64_t sequence) { current_transaction = sequence; }
uint64_t event_current_transaction(void) { return current_transaction; }

uint64_t transaction_begin(const char *actor, const char *target, uint64_t parent_event) {
    if (transaction_count >= TRANSACTION_CAPACITY) {
        return 0;
    }

    uint64_t previous_transaction = current_transaction;
    uint64_t transaction_id = next_sequence;
    char name[EVENT_LABEL_SIZE];
    transaction_name(name, sizeof(name), transaction_id);
    object_register("transaction", name, object_id("events"), "open", parent_event);

    struct transaction_record *transaction = &transactions[transaction_count++];
    transaction->id = transaction_id;
    transaction->parent_event = parent_event;
    transaction->begin_event = transaction_id;
    transaction->end_event = transaction_id;
    transaction->started_revision = object_revision(target);
    transaction->committed_revision = transaction->started_revision;
    copy_field(transaction->actor, sizeof(transaction->actor), actor);
    copy_field(transaction->target, sizeof(transaction->target), target);
    copy_field(transaction->status, sizeof(transaction->status), "open");

    current_transaction = transaction_id;
    uint64_t begin_event = event_record_kind(actor, name, "begin", EVENT_OK, parent_event,
                                             "allow:transaction", "transaction");
    current_transaction = previous_transaction;
    return begin_event;
}

uint64_t transaction_commit(uint64_t transaction_id) {
    struct transaction_record *transaction = transaction_find(transaction_id);
    if (transaction == 0) {
        return 0;
    }

    uint64_t previous_transaction = current_transaction;
    current_transaction = transaction_id;
    transaction->committed_revision = object_revision(transaction->target);
    copy_field(transaction->status, sizeof(transaction->status), "committed");

    char name[EVENT_LABEL_SIZE];
    transaction_name(name, sizeof(name), transaction_id);
    uint64_t parent = transaction->end_event;
    uint64_t commit_event = event_record_kind(transaction->actor, name, "commit", EVENT_OK, parent,
                                             "allow:transaction", "transaction");
    object_set_state_direct(name, "committed", commit_event);
    current_transaction = previous_transaction;
    return commit_event;
}

uint64_t transaction_abort(uint64_t transaction_id, const char *reason) {
    struct transaction_record *transaction = transaction_find(transaction_id);
    if (transaction == 0) {
        return 0;
    }

    uint64_t previous_transaction = current_transaction;
    current_transaction = transaction_id;
    transaction->committed_revision = object_revision(transaction->target);
    copy_field(transaction->status, sizeof(transaction->status), "aborted");

    char name[EVENT_LABEL_SIZE];
    transaction_name(name, sizeof(name), transaction_id);
    uint64_t parent = transaction->end_event;
    uint64_t abort_event = event_record_kind(transaction->actor, name, "abort", EVENT_DENIED, parent,
                                            reason, "transaction");
    object_set_state_direct(name, "aborted", abort_event);
    current_transaction = previous_transaction;
    return abort_event;
}

uint32_t event_count(void) { return count; }
const struct system_event *event_at(uint32_t index) { return index < count ? &entries[(first + index) % EVENT_CAPACITY] : 0; }

const struct system_event *event_find(uint64_t sequence) {
    for (uint32_t i = 0; i < count; i++) {
        const struct system_event *event = event_at(i);
        if (event != 0 && event->sequence == sequence) {
            return event;
        }
    }
    return 0;
}

void event_print(const struct system_event *event) {
    if (event == 0) {
        printf("Event not found.\n");
        return;
    }
    printf("#%d tx=%d kind=%s tick=%d parent=%d target_rev=%d\n",
           (int)event->sequence, (int)event->transaction_id, event->kind,
           (int)event->timestamp, (int)event->parent_event, (int)event->target_revision);
    printf("  #%d %s -> #%d %s %s %s policy=%s\n",
           (int)event->actor_object_id,
           event->actor, (int)event->target_object_id, event->target, event->operation,
           result_name(event->result), event->policy_decision);
}

void event_print_chain(uint64_t sequence) {
    const struct system_event *event = event_find(sequence);
    if (event == 0) {
        printf("Event not found: %d\n", (int)sequence);
        return;
    }
    if (event->parent_event != 0) {
        event_print_chain(event->parent_event);
    }
    event_print(event);
}

void event_print_history(const char *target) {
    uint32_t shown = 0;
    for (uint32_t i = 0; i < count; i++) {
        const struct system_event *event = event_at(i);
        uint32_t target_id = object_id(target);
        if (event != 0 && (text_equal(event->target, target) || text_equal(event->actor, target) ||
            (target_id != 0 && (event->target_object_id == target_id || event->actor_object_id == target_id)))) {
            event_print(event);
            shown++;
        }
    }
    if (shown == 0) {
        printf("No events for: %s\n", target);
    }
}

void event_print_transaction(uint64_t transaction_id) {
    const struct transaction_record *transaction = transaction_find(transaction_id);
    if (transaction == 0) {
        printf("Transaction not found: %d\n", (int)transaction_id);
        return;
    }

    printf("transaction/%d actor=%s target=%s status=%s\n",
           (int)transaction->id, transaction->actor, transaction->target, transaction->status);
    printf("parent=%d started_revision=%d committed_revision=%d begin=%d end=%d\n",
           (int)transaction->parent_event,
           (int)transaction->started_revision,
           (int)transaction->committed_revision,
           (int)transaction->begin_event,
           (int)transaction->end_event);
    printf("events:\n");
    for (uint32_t i = 0; i < count; i++) {
        const struct system_event *event = event_at(i);
        if (event != 0 && event->transaction_id == transaction_id) {
            event_print(event);
        }
    }
}

void event_print_transaction_name(const char *query) {
    const char *prefix = "transaction/";
    while (*prefix != '\0') {
        if (*query++ != *prefix++) {
            printf("Transaction not found: %s\n", query);
            return;
        }
    }
    event_print_transaction(parse_decimal64(query));
}
