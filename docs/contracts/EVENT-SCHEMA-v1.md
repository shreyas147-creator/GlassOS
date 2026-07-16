# Typed event schema v1

```text
event { sequence:u64 immutable, timestamp:u64, actor:object_ref,
target:object_ref, operation:operation, result:ok|denied|fault,
parent_event:sequence|none, policy_decision:policy_ref }
```

Sequence IDs are monotonic and never reused. The early kernel buffer retains the newest 128 records and mirrors each record to serial; later persistent journals preserve the same schema unchanged.
