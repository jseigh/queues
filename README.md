# queues
Lock-free bounded queue
## Overview
Lock-free bounded queue on an array, aka ring buffer, based on Michael-Scott algorithm.
It does not require memory management internally to avoid the ABA problem.
## Issues
Closing a queue is not synchronized with producers which may not
detect queue is closed before enqueuing to the queue.  Applications
should employ logic to ensure this does not happen, e.g. closing all
producers before closing the queue.

The can be fixed by stealing a bit from the queue node sequence number
at some point in the future, possibly then C++ gets around to properly
supporting lock-free programming.
