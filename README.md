# queues
Lock-free bounded queue
## Overview
Lock-free bounded queue on an array, aka ring buffer, based on Michael-Scott algorithm.
It does not require memory management internally to avoid the ABA problem.
## Fixes
Fixed closing of queue.  For a queue size of 2**n, the last n bits of a node sequence
where redundant since the value of those bits is the same as the node index, so one
of the bits is used to indicate the queue is closed.

Changed queue size to the more conventional capacity 
## Example test programs
These are under the test directory
### qtest
Throughput testing with a specified number of producer and consumers
```
$ ./qtest -h
  -n --count <arg>  enqueue count per producer thread (default 0)
  -t --type <arg>  queue type {mpmc, mpsc, spmc, spsc} (default mpmc)
  -p --producers <arg>  number of producer threads (default 1)
  -c --consumers <arg>  number of producer threads (default 1)
  -x --sync <name> queue enqueue/dequeue synchronization {eventcount, mutex, yield} (default eventcount)
  -s --size <arg>  queue capacity (power of 2) (default 8192)
  -q --quiet less output (default false)
  -v --verbose show config values (default false)
  -h --help show config values (default false)
```
Example - mpmc queue w/ 16 producers and 4 consumers using eventcounts for synchronization
```
$ ./qtest -n 1000000 -p 16 -c 4 -t mpmc -x eventcount
Statistics:
  producer count = 16
  consumer count = 4
  -- aggregate producer/consumer stats --
  producer enqueue count = 16,000,000 == 16,000,000 (expected)
  consumer dequeue count = 16,000,000 == 16,000,000 (expected)
  producer message sums = 7,999,992,000,000 == 7,999,992,000,000 (expected)
  consumer message sums = 7,999,992,000,000 == 7,999,992,000,000 (expected)
  producer cpu time = 14,887,862,847 nsecs
  consumer cpu time = 4,478,319,963 nsecs
  avg enqueue time =   930.4914 nsecs
  avg dequeue time =   279.8950 nsecs
  enqueue rate = 1,074,700.9268 /sec
  dequeue rate = 3,572,768.3891 /sec

  -- process stats --
  voluntary context switches = 33,896
  involuntary context switches = 2,567
  user cpu time = 18,215,851,000 nsecs
  system cpu time = 1,150,988,000 nsecs
  total cpu time =  19.3668 secs

  elapsed time =   2.4550 secs
  average enq/deq time   153.4350 nsecs
  overall rate = 6,517,418.0805 /sec

  -- client stats --
  queue full count  = 348
  queue empty count = 4148345
  producer waits    = 172
  consumer waits    = 1034630
  producer retries  = 37124190
  consumer retries  = 8469984
  producer wraps    = 551
  consumer wraps    = 30
```

### dump_queue
Interactive program to execute arbitrary enqueue and dequeue operations and show queue.
Queue has fixed capacity of 8.
```
$ ./dump_queue -h
interactive queue tester
usage: cmd <queue_type>
  where queue_type = mpmc|mpsc|spmc|spsc (default mpmc)
commands:
  enqueue <count>           -- enqueue <count values
  dequeue <count>           -- dequeue <count> times
  xchg <count>              -- paired enqueue/dequeue
  close                     -- close the queue
  show                      -- dump queue
  quit                      -- exit
  help                      -- display help
$ ./dump_queue
queue type = mpmc
mask = 7 seq_mask = fffffffffffffff8
init:
  head = 8 head.seq=8 head.ndx=0
  tail = 0 tail.seq=0 tail.ndx=0
  capacity=8 size=0 status=open
  node[00]: seq=0000 (0000) value=0
  node[01]: seq=0000 (0001) value=0
  node[02]: seq=0000 (0002) value=0
  node[03]: seq=0000 (0003) value=0
  node[04]: seq=0000 (0004) value=0
  node[05]: seq=0000 (0005) value=0
  node[06]: seq=0000 (0006) value=0
  node[07]: seq=0000 (0007) value=0


enqueue 4
command=enqueueclose
command=close

s
command=s
queue:
  head = 8 head.seq=8 head.ndx=0
  tail = 4 tail.seq=0 tail.ndx=4
  capacity=8 size=4 status=closed
  node[00]: seq=0008 (0008) value=1000
  node[01]: seq=0008 (0009) value=1001
  node[02]: seq=0008 (0010) value=1002
  node[03]: seq=0008 (0011) value=1003
  node[04]: seq=0001 (0004) value=0
  node[05]: seq=0000 (0005) value=0
  node[06]: seq=0000 (0006) value=0
  node[07]: seq=0000 (0007) value=0



enqueue 1
command=enqueue
enqueue: tail=4 tail.seq=0, ndx=4 node.seq=1 -- head=8 head.seq=8 head_ndx=0
[04] value=1004 cc=4 (closed)

enqueue: tail=0 tail.seq=0, ndx=0 node.seq=0 -- head=8 head.seq=8 head_ndx=0
[00] value=1000 cc=0 (success)
enqueue: tail=1 tail.seq=0, ndx=1 node.seq=0 -- head=8 head.seq=8 head_ndx=0
[01] value=1001 cc=0 (success)
enqueue: tail=2 tail.seq=0, ndx=2 node.seq=0 -- head=8 head.seq=8 head_ndx=0
[02] value=1002 cc=0 (success)
enqueue: tail=3 tail.seq=0, ndx=3 node.seq=0 -- head=8 head.seq=8 head_ndx=0
[03] value=1003 cc=0 (success)

show
command=show
queue:
  head = 8 head.seq=8 head.ndx=0
  tail = 4 tail.seq=0 tail.ndx=4
  capacity=8 size=4 status=open
  node[00]: seq=0008 (0008) value=1000
  node[01]: seq=0008 (0009) value=1001
  node[02]: seq=0008 (0010) value=1002
  node[03]: seq=0008 (0011) value=1003
  node[04]: seq=0000 (0004) value=0
  node[05]: seq=0000 (0005) value=0
  node[06]: seq=0000 (0006) value=0
  node[07]: seq=0000 (0007) value=0
close
command=close

show
command=show
queue:
  head = 8 head.seq=8 head.ndx=0
  tail = 4 tail.seq=0 tail.ndx=4
  capacity=8 size=4 status=closed
  node[00]: seq=0008 (0008) value=1000
  node[01]: seq=0008 (0009) value=1001
  node[02]: seq=0008 (0010) value=1002
  node[03]: seq=0008 (0011) value=1003
  node[04]: seq=0001 (0004) value=0
  node[05]: seq=0000 (0005) value=0
  node[06]: seq=0000 (0006) value=0
  node[07]: seq=0000 (0007) value=0


enqueue 1
command=enqueue
enqueue: tail=4 tail.seq=0, ndx=4 node.seq=1 -- head=8 head.seq=8 head_ndx=0
[04] value=1004 cc=4 (closed)
```

The paranthesized number is the node sequence + node ndex w/o the close bit.

