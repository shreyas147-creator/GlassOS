# Canonical object model v1

Every object has a stable typed ID, current state, metadata, capability set, and causal-event references. `process` owns threads and capabilities; `thread` is an execution context; `file` is named immutable-or-versioned bytes; `device` is a hardware endpoint; `socket` is an endpoint pair; `config_key` is a versioned setting; `capability` grants a typed operation over a target; and `event` is an immutable action record. A `causal_edge` is `parent_event -> child_event`, meaning the parent directly enabled or caused the child.

For every system action GlassOS records: immutable sequence ID, timestamp, actor, target, operation, result, parent event, policy decision, and any object-state revision affected. This is the source of truth for both auditing and inspection.
