# The master's web port

The Rust master owns the web port: `webserver_interface`, `webserver_port` and the TLS flags
(`webserver_certificate_file`, `webserver_private_key_file`, `webserver_private_key_password`,
`webserver_ssl_ciphers`, `webserver_strict_transport_security`). `common/yb-web` binds every address
the interface resolves to, as the C++ `Webserver` does, and serves HTTP or HTTPS (OpenSSL, TLS 1.2
minimum).

The pages are still the C++ ones. `CreateMasterHost` moves the host's C++ `Webserver` to an
ephemeral port on 127.0.0.1 without TLS, and `yb-web` proxies every path it does not serve itself
there. So `/varz`, `/metrics`, `/prometheus-metrics`, logs, rpcz, pprof and the rest are the C++
server's own output, and the master pages are the ones a C++ follower serves, registered by
`MasterHost::RegisterWebPages` with the same paths, titles and nav bar entries as
`MasterPathHandlers::Register`:

| Pages | Served from |
|---|---|
| `/`, `/tables`, `/tablet-servers`, `/api/v1/tables` and the rest (every `RegisterLeaderOrRedirect` page) | Fetched from the leader with `master::RedirectToLeader`, as a C++ follower does |
| `/masters`, `/api/v1/masters` | `master::RenderMastersPage` / `RenderMastersJson` over the master list |
| `/api/v1/is-leader` | 503, the C++ non-leader answer |
| `/api/v1/version`, `/api/v1/meta-cache` | The host |

The functions in `master/master-path-handlers.h` are the C++ master's own code, moved out of
`MasterPathHandlers` so both masters call them. The master list comes from Rust: the host calls
`WebMasterList::list_masters` (`master/master/src/web.rs`), the same fan-out as the `ListMasters`
RPC. The leader's address and scheme come from the same flags as on a C++ follower
(`master_webserver_require_https`, `webserver_ca_certificate_file`).

The host advertises the Rust port: `GetRegistration` fills `http_addresses` from the configured
interface and port, or from the bound addresses for a wildcard interface or port 0, and
`GetStatusPB` (the server info dump tests read) reports the bound addresses.

Not supported yet, and refused at startup rather than served without them:
`webserver_password_file`, `webserver_authentication_domain` and
`webserver_redirect_http_to_https`.
