# Expand contract v1

`Expand(object)` always returns: (1) current typed state and revision; (2) immediate causal events and related objects; (3) available next expansions, such as owner, children, capabilities, history, or policy; and (4) mutations that are available with the capability and policy decision required for each.

No mutation is implied by Expand. A mutation requires an explicit action request and produces a new event whose `parent_event` is the inspected or authorizing event when applicable.
