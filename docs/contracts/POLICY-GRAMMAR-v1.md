# Capability and policy grammar v1

```text
capability := grant subject object operation [constraint]
policy     := allow|deny subject operation object [when predicate]
transaction := begin revision; change+; journal event; commit revision
```

Policy files are human-readable, versioned under `policy/`, and changed only through a journaled transaction. Enforcement must report the policy revision and rule in `policy_decision`.
