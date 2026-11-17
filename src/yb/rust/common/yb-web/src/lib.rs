// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

//! A server's public web port.
//!
//! Rust owns the port (`webserver_interface`, `webserver_port`, the TLS flags and Digest
//! authentication). Routes the server implements in Rust are served here; every other path is
//! proxied to the host's C++ `Webserver`, which listens on an ephemeral 127.0.0.1 port and keeps
//! serving the pages it always has: `/varz`, `/metrics`, `/prometheus-metrics`, logs, threads,
//! mem-trackers, rpcz, pprof and the pages the host registers itself.

mod auth;
mod pass;
mod proxy;
mod tls;

use std::net::{SocketAddr, ToSocketAddrs};
use std::sync::Arc;

use axum::extract::Request;
use axum::http::{header, HeaderValue};
use axum::middleware::{from_fn_with_state, map_response};
use axum::response::Response;
use axum::Router;
use tokio::task::JoinSet;
use yb_host_api::{Status, StatusCode};

pub use auth::AuthSettings;
pub use pass::PassToHost;
pub use tls::TlsSettings;

/// Bound but not yet serving listeners, so the server can advertise its address before it starts
/// serving.
pub struct WebListener {
    listeners: Vec<std::net::TcpListener>,
    tls: Option<openssl::ssl::SslAcceptor>,
    redirect_http_to_https: bool,
    strict_transport_security: Option<fn() -> bool>,
    auth: Option<Arc<auth::DigestAuth>>,
}

fn io_error(context: &str, e: std::io::Error) -> Status {
    Status::new(StatusCode::NetworkError, format!("{context}: {e}"))
}

impl WebListener {
    /// Binds every address `host` resolves to, on `port`, as the C++ web server does; an empty host
    /// binds every interface. With port 0, all addresses share the port the first one gets. A bad
    /// certificate or key, or a missing password file, fails here, at startup. With `auth`, every
    /// path needs credentials.
    pub fn bind(
        host: &str,
        port: u16,
        tls: Option<&TlsSettings>,
        auth: Option<&AuthSettings>,
    ) -> Result<WebListener, Status> {
        let strict_transport_security = tls.and_then(|t| t.strict_transport_security);
        let redirect_http_to_https = tls.is_some_and(|t| t.redirect_http_to_https);
        let tls = tls.map(tls::acceptor).transpose()?;
        let auth = auth.map(auth::DigestAuth::new).transpose()?.map(Arc::new);
        let host = if host.is_empty() { "0.0.0.0" } else { host };
        let context = format!("binding the web server to {host}:{port}");
        let mut addrs: Vec<SocketAddr> = Vec::new();
        for addr in (host, port)
            .to_socket_addrs()
            .map_err(|e| io_error(&context, e))?
        {
            if !addrs.contains(&addr) {
                addrs.push(addr);
            }
        }
        let mut listeners = Vec::with_capacity(addrs.len());
        let mut port = port;
        for addr in addrs {
            let listener = std::net::TcpListener::bind(SocketAddr::new(addr.ip(), port))
                .map_err(|e| io_error(&context, e))?;
            listener
                .set_nonblocking(true)
                .map_err(|e| io_error(&context, e))?;
            port = listener
                .local_addr()
                .map_err(|e| io_error(&context, e))?
                .port();
            listeners.push(listener);
        }
        if listeners.is_empty() {
            return Err(Status::new(
                StatusCode::ConfigurationError,
                format!("No IPs available for address {host}:{port}"),
            ));
        }
        Ok(WebListener {
            listeners,
            tls,
            redirect_http_to_https,
            strict_transport_security,
            auth,
        })
    }

    /// The bound addresses, all on the same port.
    pub fn local_addrs(&self) -> Vec<SocketAddr> {
        self.listeners
            .iter()
            .map(|l| l.local_addr().expect("a bound listener has an address"))
            .collect()
    }

    /// Serves `routes`, and proxies every other path to the C++ web server on
    /// `127.0.0.1:loopback_port`, until the task is dropped or a listener fails. Must run on a
    /// tokio runtime.
    pub async fn serve(self, routes: Router, loopback_port: u16) -> Result<(), Status> {
        let proxy = Arc::new(proxy::LoopbackProxy::new(loopback_port));
        let routes = routes.layer(from_fn_with_state(proxy.clone(), pass::forward_passed));
        let mut router = routes.fallback(move |request: Request| {
            let proxy = proxy.clone();
            async move { proxy.forward(request).await }
        });
        if let Some(auth) = self.auth {
            router = router.layer(from_fn_with_state(auth, auth::require_digest));
        }
        if let Some(enabled) = self.strict_transport_security {
            // As the C++ web server sends it when it serves HTTPS itself.
            router = router.layer(map_response(move |mut response: Response| async move {
                if enabled() {
                    response.headers_mut().insert(
                        header::STRICT_TRANSPORT_SECURITY,
                        HeaderValue::from_static("max-age=31536000"),
                    );
                }
                response
            }));
        }
        let mut servers = JoinSet::new();
        for listener in self.listeners {
            let listener = tokio::net::TcpListener::from_std(listener)
                .map_err(|e| Status::new(StatusCode::IoError, e.to_string()))?;
            let router = router.clone();
            let tls = self.tls.clone();
            let redirect_http_to_https = self.redirect_http_to_https;
            servers.spawn(async move {
                match tls {
                    None => axum::serve(listener, router)
                        .await
                        .map_err(|e| Status::new(StatusCode::IoError, e.to_string())),
                    Some(acceptor) => {
                        tls::serve(listener, router, acceptor, redirect_http_to_https).await
                    }
                }
            });
        }
        while let Some(result) = servers.join_next().await {
            result.map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))??;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests;
