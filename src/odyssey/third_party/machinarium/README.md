## Machinarium

Machinarium allows to create fast networked and event-driven asynchronous applications in
synchronous/procedural manner instead of using traditional callback approach.

*Public API ⇨*  [sources/machinarium.h](sources/machinarium.h)

#### Threads and coroutines

Machinarium is based on combination of `pthreads(7)` and custom made implementation of efficient cooperative
multi-tasking primitives (coroutines).

Each coroutine executed using own stack context and transparently scheduled by `epoll(7)` event-loop logic.
Each working Machinarium thread can handle thousands of executing coroutines.

#### Messaging and Channels

Machinarium messages and channels are used to provide IPC between threads and
coroutines. Ideally, this approach should be sufficient to fulfill needs of most multi-threaded applications
without the need of using additional access synchronization.

#### Efficient TCP/IP networking

Machinarium IO API primitives can be used to develop high-performance client and server applications.
By doing blocking IO calls, currently executing coroutine suspends and switches
to a next one ready to go. When the original request completed, timedout or cancel event happened,
coroutine resumes.

One of the main goals of networking API design is performance. To reduce number of system calls
read operation implemented with readahead support. It is fully buffered and transparently continue to read
socket data even when no active calls are in progress, to reduce `epoll(7)` subscribe overhead.

Machinarium IO contexts can be transferred between threads, which allows to develop efficient
producer-consumer network applications.

#### Full-featured SSL/TLS support

Machinarium has easy-to-use Transport Layer Security (TLS) API methods.

Create Machinarium TLS object, associate it with any existing IO context and the connection
will be automatically upgraded.

#### DNS resolving

Machinarium implements separate thread-pool to run network address
and service translation functions such as `getaddrinfo()`, to avoid process blocking and to be
consistent with coroutine design.

#### Timeouts and Cancellation

All blocking Machinarium API methods are designed with timeout flag. If operation does not
complete during requested time interval, then method will return with appropriate `errno` status set.

Additionally, a coroutine can `Cancel` any on-going blocking call of some other coroutine. And as in
timeout handling, cancelled coroutine method will return with appropriate status.
