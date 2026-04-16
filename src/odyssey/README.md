<p align="center">
	<img src="documentation/odyssey.png" width="35%" height="35%" /><br>
</p>
<br>

## Odyssey

Advanced multi-threaded PostgreSQL connection pooler and request router.

#### Project status

Odyssey is production-ready, it is being used in large production setups. We appreciate any kind of feedback and contribution to the project.

<a href="https://travis-ci.org/yandex/odyssey"><img src="https://travis-ci.org/yandex/odyssey.svg?branch=master" /></a>

<a href="https://scan.coverity.com/projects/yandex-odyssey">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/20374/badge.svg"/>
</a>

### Design goals and main features

#### Multi-threaded processing

Odyssey can significantly scale processing performance by
specifying a number of additional worker threads. Each worker thread is
responsible for authentication and proxying client-to-server and server-to-client
requests. All worker threads are sharing global server connection pools.
Multi-threaded design plays important role in `SSL/TLS` performance.

#### Advanced transactional pooling

Odyssey tracks current transaction state and in case of unexpected client
disconnection can emit automatic `Cancel` connection and do `Rollback` of
abandoned transaction, before putting server connection back to
the server pool for reuse. Additionally, last server connection owner client
is remembered to reduce a need for setting up client options on each
client-to-server assignment.

#### Better pooling control

Odyssey allows to define connection pools as a pair of `Database` and `User`.
Each defined pool can have separate authentication, pooling mode and limits settings.

#### Authentication

Odyssey has full-featured `SSL/TLS` support and common authentication methods
like: `md5` and `clear text` both for client and server authentication. 
Odyssey supports PAM & LDAP authentication, this methods operates similarly to `clear text` auth except that it uses 
PAM/LDAP to validate user name/password pairs. PAM optionally checks the connected remote host name or IP address.
Additionally it allows to block each pool user separately.

#### Logging

Odyssey generates universally unique identifiers `uuid` for client and server connections.
Any log events and client error responses include the id, which then can be used to
uniquely identify client and track actions. Odyssey can save log events into log file and
using system logger.

### CLI

Odyssey supports multiple command line options. Use `/path/to/odyssey` --help to see more

#### Architecture and internals

Odyssey has sophisticated asynchronous multi-threaded architecture which
is driven by custom made coroutine engine: [machinarium](https://github.com/yandex/odyssey/tree/master/third_party/machinarium).
Main idea behind coroutine design is to make event-driven asynchronous applications to look and feel
like being written in synchronous-procedural manner instead of using traditional
callback approach.

One of the main goal was to make code base understandable for new developers and
to make an architecture easily extensible for future development.

More information: [Architecture and internals](documentation/internals.md).

#### Build instructions

Currently Odyssey runs only on Linux. Supported platforms are x86/x86_64.

To build you will need:

* cmake >= 3.12.4
* gcc >= 4.6
* openssl
* postgresql-server-dev-13
* pg_config utility is in the PATH

```sh
git clone git://github.com/yandex/odyssey.git
cd odyssey
make local_build
```
Adapt odyssey-dev.conf then:
```sh
make local_run
```

Alternatively:
```sh
make console_run
```

#### Use docker environment for development (helpful for Mac users)
```sh
make start-dev-env
```
Set up your CLion to build project in container, [manual](https://github.com/shuhaoliu/docker-clion-dev/blob/master/README.md).

### Configuration reference

##### Service

* [include](documentation/configuration.md#include-string)
* [daemonize](documentation/configuration.md#daemonize-yesno)
* [priority](documentation/configuration.md#priority-integer)
* [pid\_file](documentation/configuration.md#pid_file-string)
* [unix\_socket\_dir](documentation/configuration.md#unix_socket_dir-string)
* [unix\_socket\_mode](documentation/configuration.md#unix_socket_mode-string)

##### Logging

* [log\_file](documentation/configuration.md#log_file-string)
* [log\_format](documentation/configuration.md#log_format-string)
* [log\_to\_stdout](documentation/configuration.md#log_to_stdout-yesno)
* [log\_syslog](documentation/configuration.md#log_syslog-yesno)
* [log\_syslog\_ident](documentation/configuration.md#log_syslog_ident-string)
* [log\_syslog\_facility](documentation/configuration.md#log_syslog_facility-string)
* [log\_debug](documentation/configuration.md#log_debug-yesno)
* [log\_config](documentation/configuration.md#log_config-yesno)
* [log\_session](documentation/configuration.md#log_session-yesno)
* [log\_query](documentation/configuration.md#log_query-yesno)
* [log\_stats](documentation/configuration.md#log_stats-yesno)
* [stats\_interval](documentation/configuration.md#stats_interval-integer)

##### Performance

* [workers](documentation/configuration.md#workers-integer)
* [resolvers](documentation/configuration.md#resolvers-integer)
* [readahead](documentation/configuration.md#readahead-integer)
* [cache\_coroutine](documentation/configuration.md#cache_coroutine-integer)
* [nodelay](documentation/configuration.md#nodelay-yesno)
* [keepalive](documentation/configuration.md#keepalive-integer)

##### System

* [coroutine\_stack\_size](documentation/configuration.md#coroutine_stack_size-integer)

##### Global limits

* [client\_max](documentation/configuration.md#client_max-integer)

##### Listen

* [host](documentation/configuration.md#host-string)
* [port](documentation/configuration.md#port-integer)
* [backlog](documentation/configuration.md#backlog-integer)
* [tls](documentation/configuration.md#tls-string)
* [tls\_ca\_file](documentation/configuration.md#tls-string)
* [tls\_key\_file](documentation/configuration.md#tls-string)
* [tls\_cert\_file](documentation/configuration.md#tls-string)
* [tls\_protocols](documentation/configuration.md#tls-string)
* [example](documentation/configuration.md#example)

##### Routing

* [overview](documentation/configuration.md#routing-rules)

##### Storage

* [overview](documentation/configuration.md#storage)
* [type](documentation/configuration.md#type-string)
* [host](documentation/configuration.md#host-string-1)
* [port](documentation/configuration.md#port-integer-1)
* [tls](documentation/configuration.md#tls-string-1)
* [tls\_ca\_file](documentation/configuration.md#tls-string-1)
* [tls\_key\_file](documentation/configuration.md#tls-string-1)
* [tls\_cert\_file](documentation/configuration.md#tls-string-1)
* [tls\_protocols](documentation/configuration.md#tls-string-1)
* [example](documentation/configuration.md#example-1)

##### Database and user

* [overview](documentation/configuration.md#database-and-user)
* [authentication](documentation/configuration.md#authentication-string)
* [password](documentation/configuration.md#password-string)
* [auth\_common\_name](documentation/configuration.md#auth_common_name-defaultstring)
* [auth\_query](documentation/configuration.md#auth_query-string)
* [auth\_query\_db](documentation/configuration.md#auth_query-string)
* [auth\_query\_user](documentation/configuration.md#auth_query-string)
* [auth\_pam\_service](documentation/configuration.md#auth\_pam\_service-string)
* [client\_max](documentation/configuration.md#client_max-integer-1)
* [storage](documentation/configuration.md#storage-string)
* [storage\_db](documentation/configuration.md#storage-string)
* [storage\_user](documentation/configuration.md#storage-string)
* [storage\_password](documentation/configuration.md#storage-string)
* [password\_passthrough](documentation/configuration.md#storage-string)
* [pool](documentation/configuration.md#pool-string)
* [pool\_size](documentation/configuration.md#pool_size-integer)
* [pool\_timeout](documentation/configuration.md#pool_timeout-integer)
* [pool\_ttl](documentation/configuration.md#pool_ttl-integer)
* [pool\_discard](documentation/configuration.md#pool_discard-yesno)
* [pool\_cancel](documentation/configuration.md#pool_cancel-yesno)
* [pool\_rollback](documentation/configuration.md#pool_rollback-yesno)
* [client\_fwd\_error](documentation/configuration.md#client_fwd_error-yesno)
* [log\_debug](documentation/configuration.md#log_debug-yesno-1)
* [example](documentation/configuration.md#example-remote)
* [example console](documentation/configuration.md#example-admin-console)

##### Architecture and Internals

* [overview](documentation/internals.md)
* [error codes](documentation/internals.md#client-error-codes)
