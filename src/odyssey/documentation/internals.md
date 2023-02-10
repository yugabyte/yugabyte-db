
### Odyssey architecture and internals

Odyssey heavily depends on two libraries, which were originally created during its
development: Machinarium and Kiwi.

#### Machinarium

Machinarium extensively used for organization of multi-thread processing, cooperative multi-tasking
and networking IO. All Odyssey threads are run in context of machinarium `machines` -
pthreads with coroutine schedulers placed on top of `epoll(7)` event loop.

Odyssey does not directly use or create multi-tasking primitives such as OS threads and mutexes.
All synchronization is done using message passing and transparently handled by machinarium.

Repository: [third\_party/machinarium](https://github.com/yandex/odyssey/tree/master/third_party/machinarium)

#### Kiwi

Kiwi provides functions for constructing, reading and validating
PostgreSQL protocol requests messages. By design, all PostgreSQL specific details should be provided by
Kiwi library.

Repository: [third\_party/kiwi](https://github.com/yandex/odyssey/tree/master/third_party/kiwi)

#### Core components

```
                                              main()
                                           .----------.
                                           | instance |
                           thread          '----------'
                         .--------.                          .-------------.
                         | system |                          | worker_pool |
                         '--------'                          '-------------'
                  .--------.    .---------.           .---------.         .---------.
                  | router |    | servers |           | worker0 |   ...   | workerN |
                  '--------'    '---------'           '---------'         '---------'
                  .---------.    .------.               thread              thread
                  | console |    | cron |
                  '---------'    '------'
```

#### Instance

Application entry point.

Handle initialization. Read configuration file, prepare loggers, parse cli options.
Run system and worker\_pool threads.

[sources/instance.h](/sources/instance.h), [sources/instance.c](/sources/instance.c)

#### System

Start router, cron and console subsystems.

Create listen server one for each resolved address. Each listen server runs inside own coroutine.
Server coroutine mostly waits on `machine_accept()`.

On incoming connection, new client context is created and notification message is sent to next
worker using `workerpool_feed()`. Client IO context is not attached to any `epoll(7)` context yet.

Handle signals using `machine_signal_wait()`. On `SIGHUP`: do versional config reload, add new databases
and obsolete old ones. On `SIGUSR1`: reopen log file. On `SIGUSR2`: graceful shutdown.
On `SIGINT`, `SIGTERM`: call `exit(3)`. Other threads are blocked from receiving signals.

[sources/system.h](/sources/system.h), [sources/system.c](/sources/system.c)

#### Router

Handle client registration and routing requests. Do client-to-server attachment and detachment.
Ensure connection limits and client pool queueing. Handle implicit `Cancel` client request, since access
to server pool is required to match a client key.

Router works in request-reply manner: client (from worker thread) sends a request message to
router and waits for reply. Could be a potential hot spot (not an issue at the moment).

[sources/router.h](/sources/router.h), [sources/router.c](/sources/router.c)

#### Cron

Do periodic service tasks, like idle server connection expiration and
database config obsoletion.

[sources/cron.h](/sources/cron.h), [sources/cron.c](/sources/cron.c)

#### Worker and worker pool

Worker thread (machinarium machine) waits on incoming connection notification queue. On new connection event,
create new frontend coroutine and handle client (frontend) lifecycle. Each worker thread can host
thousands of client coroutines.

Worker pool is responsible for maintaining a thread pool of workers. Threads are machinarium machines,
created using `machine_create()`.

[sources/worker.h](/sources/worker.h), [sources/worker.c](/sources/worker.c),
[sources/worker_pool.h](/sources/worker_pool.h), [sources/worker_pool.c](/sources/worker_pool.c)

#### Single worker mode

To reduce multi-thread communication overhead, Odyssey handles case with a single worker (`workers 1`)
differently.

Instead of creating separate thread + coroutine for each worker, only one worker coroutine created inside system thread. All message channels `machine_channel_create()` created marked as non-shared. This allows to make faster communications without a need to do expensive system calls for event loop wakeup.

#### Client (frontend) lifecycle

Whole client logic is driven by a single `od_frontend()` function, which is a coroutine entry point.
There are 6 distinguishable stages in client lifecycle.

[sources/frontend.h](/sources/frontend.h), [sources/frontend.c](/sources/frontend.c)

#### 1. Startup

Read initial client request. This can be `SSLRequest`, `CancelRequest` or `StartupMessage`.
Handle SSL/TLS handshake.

#### 2. Process Cancel request

In case of `CancelRequest`, call Router to handle it. Disconnect client right away.

#### 3. Route client

Call router. Use `Database` and `User` to match client configuration route. Router assigns
matched route to a client. Each route object has a reference counter.
All routes are periodically garbage-collected.

#### 4. Authenticate client

Write client an authentication request `AuthenticationMD5Password` or `AuthenticationCleartextPassword` and
wait for reply to compare passwords. In case of success send `AuthenticationOk`.

#### 5. Process client requests

Depending on selected route storage type, do `local` (console) or `remote` (remote PostgreSQL server) processing.

Following remote processing logic repeats until client sends `Terminate`,
client or server disconnects during the process:

* Read client request. Handle `Terminate`.
* If client has no server attached, call Router to assign server from the server pool. New server connection registered and
initiated by the client coroutine (worker thread). Maybe discard previous server settings and configure it using client parameters.
* Send client request to the server.
* Wait for server reply.
* Send reply to client.
* In case of `Transactional` pooling: if transaction completes, call Router to detach server from the client.
* Repeat.

#### 6. Cleanup

If server is not Ready (query still in-progress), initiate automatic `Cancel` procedure. If server is Ready and left in active transaction,
initiate automatic `Rollback`. Return server back to server pool or disconnect.

Free client context.

#### Client error codes

In the most scenarios PostgreSQL error messages `ErrorResponce` are copied to a client as-is. Yet, there are some
cases, when Odyssey has to provide its own error message and SQLCode to client.

Function `od_frontend_error()` is used for formatting and sending error message to client.

| SQLCode | PostgreSQL Code Name | Stage |
| ------- | -------------------- | ----- |
| **08P01** | `PROTOCOL_VIOLATION` | Startup, TLS handshake, authentication |
| **0A000** | `FEATURE_NOT_SUPPORTED` | TLS handshake |
| **28000** | `INVALID_AUTHORIZATION_SPECIFICATION`  | Authentication |
| **28P01** | `INVALID_PASSWORD` | Authentication |
| **58000** | `SYSTEM_ERROR` | Routing, System specific |
| **3D000** | `UNDEFINED_DATABASE` | Routing |
| **53300** | `TOO_MANY_CONNECTIONS` | Routing |
| **08006** | `CONNECTION_FAILURE` | Server-side error during connection or IO |

PostgreSQL specific error codes can be found in `src/backend/errocodes.txt`.
