# Synchronization Notes



## Sequence number -
Sequence numbers are effectively monotonic and store ordering can be
inferred from them.

## Tail -
The tail sequence number value is always less than or equal to the next available
empty node.  If less than, a scan is done for the next available empty node.  The
update of the tail won't be visible before the update of the node even if it's a
relaxed store since there is a control dependency on the atomic update of the node.

## 128 bit load/store
Atomic 128 bit loads aren't supported by c++, so load is implemented by
doing a load acquire on the sequence number and then a load on the value.
Any atomic updates will fail if sequence number is not current.

## Enqueue head sequence number visibility
The enqueue node is gotten via either the tail if it is from the last enqueue, or via a scan for possible empty
node following the last enqueued node.  An acquire fence ensures that the head value is at least as current as
that seen by last enqueue.  Since a successful equeue saw head > tail, an acquire fence guarantees head >= tail.

## Pointer value acquire/release
The acquire/release semantics are on the enqueue/dequeue api, and are not necessarily required on the actual pointer
value accesses.
* An enqueue means a release fence happens before the actual store.
* A dequeue means an acquire fence happens after the actual load.