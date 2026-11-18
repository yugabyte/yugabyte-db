# The master's web port

The Rust master owns the web port: `webserver_interface`, `webserver_port` and the TLS flags
(`webserver_certificate_file`, `webserver_private_key_file`, `webserver_private_key_password`,
`webserver_ssl_ciphers`, `webserver_strict_transport_security`). `common/yb-web` binds every address
the interface resolves to, as the C++ `Webserver` does, and serves HTTP or HTTPS (OpenSSL, TLS 1.2
minimum).

`CreateMasterHost` moves the host's C++ `Webserver` to an ephemeral port on 127.0.0.1 without
TLS, and `yb-web` proxies every path the Rust routes do not serve there. So `/varz`, `/metrics`,
`/prometheus-metrics`, logs, rpcz, pprof and the rest are the C++ server's own output.

## Leader pages

`master/web` (`master-web`) serves the leader pages of `MasterPathHandlers::Register` on a ready
leader. Every page is a function of one `View`: a catalog snapshot, the tserver registry and the
observed replica locations, taken when the request arrives, rendered on a blocking thread. On a
master that is not a ready leader a route answers `yb_web::PassToHost` and the request goes to the
host, whose `MasterHost::RegisterWebPages` fetches the page from the leader with
`master::RedirectToLeader`, as a C++ follower does.

| Pages | Served from |
|---|---|
| `/api/v1/tablet-servers`, `/api/v1/health-check`, `/api/v1/tablet-replication`, `/api/v1/tablet-under-replication`, `/dump-entities`, `/api/v1/namespaces`, `/api/v1/table`, `/api/v1/tables`, `/api/v1/cluster-config`, `/api/v1/xcluster`, `/api/v1/stateful-services` | `master-web`, the C++ leader's bytes |
| `/api/v1/overview`, `/api/v1/load-distribution`, `/api/v1/tasks`, `/api/v1/tablet-server-clocks` | `master-web`; only a Rust leader has them (the UI's data for pages C++ renders as HTML) |
| `/`, `/tablet-servers`, `/tablet-server-clocks`, `/tables`, `/namespaces`, `/table`, `/cluster-config`, `/xcluster`, `/tasks`, `/tablet-replication`, `/load-distribution`, `/stateful-services` | The web UI's `index.html`, on every master |
| `/ObjectLockManager` | The host: the leader's lock manager is the host's |
| `/masters`, `/api/v1/masters` | `master::RenderMastersPage` / `RenderMastersJson` over the master list |
| `/api/v1/is-leader` | The host: `{"STATUS":"OK"}` on the leader, 503 elsewhere |
| `/api/v1/version`, `/api/v1/meta-cache` | The host |

The C++ JSON pages are an external contract (YBA, yugabyted-ui, scripts), so `master-web`'s
`json.rs` writes what the C++ `JsonWriter` (rapidjson) writes: the same separators, escapes and
number formats, objects left open where a C++ handler returns early, and `JsonWriter::Protobuf`'s
field order (set fields by number). Responses are 200 `text/plain`, as the C++ server answers
every handler. Arrays the C++ handlers fill from unordered maps (namespaces, tservers, placements)
are sorted by id. Where only C++ code can render a value (column types and partition bounds of
`/api/v1/table`), `master-web` calls the host method `WebTableLayout`. `RustMasterTest.RustLeaderServesWebJson`
diffs every C++ JSON page between a C++ leader and the Rust leader that follows it.

`/api/v1/table` lists the table's tserver RPC tasks in flight (create, delete, alter and truncate
of its tablets, and the balancer's), from the `TableTasks` registry in `master-task-runtime`.
`/api/v1/load-distribution` asks every live tserver for its remote bootstraps (`GetActiveRbsInfo`,
5 s), and its tasks summary shows the latest balancer run's tasks with their live states. Unlike
C++, the table page does not show split parents already deleted: the Rust catalog drops a deleted
tablet's row, while C++ keeps the deleted TabletInfo in memory until a restart.

`/api/v1/tablet-replication` follows the leaderless check of `master-tablet-locations` (K7): while a
leader's tserver is live, a tablet's leaderless time ends at that tserver's last heartbeat, so a
tablet whose leader just stopped heartbeating is reported only once the tserver is marked
unresponsive, later than C++ reports it.

## The web UI

`master/web/ui` is a React and TypeScript app built with Vite. Every old page URL answers its
`index.html`, its files are under `/ui/`, and it reads only the JSON pages, so a follower's UI
shows the leader's data through the host's fetch. It is built the way yugabyted's UI is:
`yb_build.sh --build-master-ui` runs `ui/build.sh` (`npm ci`, `vite build`) into
`$BUILD_ROOT/master-ui` before the C++ and Rust build, and `master/web/build.rs` embeds what
`YB_MASTER_UI_DIR` names. A binary built without it serves the JSON pages, and at the page URLs a
leader says the UI was not built while a follower fetches the leader's page.

The functions in `master/master-path-handlers.h` are the C++ master's own code, moved out of
`MasterPathHandlers` so both masters call them. The master list comes from Rust: the host calls
`WebMasterList::list_masters` (`master/server/src/web.rs`), the same fan-out as the `ListMasters`
RPC. The leader's address and scheme come from the same flags as on a C++ follower
(`master_webserver_require_https`, `webserver_ca_certificate_file`).

The host advertises the Rust port: `GetRegistration` fills `http_addresses` from the configured
interface and port, or from the bound addresses for a wildcard interface or port 0, and
`GetStatusPB` (the server info dump tests read) reports the bound addresses.

Authentication and the HTTPS redirect follow squeasel, the C++ server's HTTP library:

- `webserver_password_file` turns on HTTP Digest authentication for every path, Rust routes and
  proxied ones alike. The file is htdigest format (`user:realm:md5(user:realm:password)`), checked
  at startup (missing: `InvalidArgument`) and read on every request. The realm is
  `webserver_authentication_domain`, `mydomain.com` when empty; the domain alone does nothing. The
  loopback C++ server runs without either, since `yb-web` authenticates in front of it, so a
  process on the same host can still read the proxied pages through its ephemeral 127.0.0.1 port
  without credentials (the C++ server cannot keep the file: see below). Unlike
  squeasel, an unreadable file refuses requests instead of admitting them, and PUT and DELETE need
  credentials too. A follower's `RedirectToLeader` fetch sends no credentials, so with
  authentication on the leader those pages show "HTTP 401". The C++ `Webserver` hands squeasel the
  file as `global_passwords_file`, an option squeasel rejects (it expects `global_auth_file`), so
  a C++ server with this flag fails to start.
- `webserver_redirect_http_to_https`, with TLS on, answers plain HTTP on the web port (the first
  byte is not a TLS handshake record) with `302` to `https://<Host without port>:<port><path>`,
  before authentication. Without the flag such connections are closed.
