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

//! HTTPS with OpenSSL, the same library the rest of the server uses, configured from the C++ web
//! server's flags (webserver_options.cc).

use std::pin::Pin;
use std::sync::Arc;

use axum::Router;
use hyper_util::rt::{TokioExecutor, TokioIo};
use hyper_util::server::conn::auto;
use hyper_util::service::TowerToHyperService;
use openssl::pkey::PKey;
use openssl::ssl::{Ssl, SslAcceptor, SslFiletype, SslMethod, SslVersion};
use tokio::net::TcpListener;
use tokio_openssl::SslStream;
use yb_host_api::{Status, StatusCode};

/// `webserver_certificate_file` and friends.
#[derive(Clone, Debug, Default)]
pub struct TlsSettings {
    /// PEM certificate chain.
    pub certificate_file: String,
    /// PEM private key; read from the certificate file when empty.
    pub private_key_file: String,
    /// Password of the private key.
    pub private_key_password: String,
    /// OpenSSL cipher list; OpenSSL's default when empty.
    pub ciphers: String,
    /// Whether to send `Strict-Transport-Security` (`webserver_strict_transport_security`); read
    /// per response, so a runtime flag change applies.
    pub strict_transport_security: Option<fn() -> bool>,
}

fn tls_error(e: impl std::fmt::Display) -> Status {
    Status::new(
        StatusCode::ConfigurationError,
        format!("web server TLS: {e}"),
    )
}

pub(crate) fn acceptor(settings: &TlsSettings) -> Result<SslAcceptor, Status> {
    let mut builder = SslAcceptor::mozilla_intermediate_v5(SslMethod::tls()).map_err(tls_error)?;
    // webserver_ssl_min_version only accepts tlsv1.2.
    builder
        .set_min_proto_version(Some(SslVersion::TLS1_2))
        .map_err(tls_error)?;
    if !settings.ciphers.is_empty() {
        builder
            .set_cipher_list(&settings.ciphers)
            .map_err(tls_error)?;
    }
    builder
        .set_certificate_chain_file(&settings.certificate_file)
        .map_err(tls_error)?;
    let key_file = if settings.private_key_file.is_empty() {
        &settings.certificate_file
    } else {
        &settings.private_key_file
    };
    if settings.private_key_password.is_empty() {
        builder
            .set_private_key_file(key_file, SslFiletype::PEM)
            .map_err(tls_error)?;
    } else {
        let pem = std::fs::read(key_file).map_err(tls_error)?;
        let key =
            PKey::private_key_from_pem_passphrase(&pem, settings.private_key_password.as_bytes())
                .map_err(tls_error)?;
        builder.set_private_key(&key).map_err(tls_error)?;
    }
    builder.check_private_key().map_err(tls_error)?;
    Ok(builder.build())
}

pub(crate) async fn serve(
    listener: TcpListener,
    router: Router,
    acceptor: SslAcceptor,
) -> Result<(), Status> {
    let acceptor = Arc::new(acceptor);
    loop {
        let (stream, peer) = listener
            .accept()
            .await
            .map_err(|e| Status::new(StatusCode::IoError, e.to_string()))?;
        let acceptor = acceptor.clone();
        let router = router.clone();
        tokio::spawn(async move {
            let ssl = match Ssl::new(acceptor.context()) {
                Ok(ssl) => ssl,
                Err(e) => return tracing::warn!("TLS session for {peer}: {e}"),
            };
            let mut stream = match SslStream::new(ssl, stream) {
                Ok(stream) => stream,
                Err(e) => return tracing::warn!("TLS session for {peer}: {e}"),
            };
            if let Err(e) = Pin::new(&mut stream).accept().await {
                return tracing::debug!("TLS handshake with {peer}: {e}");
            }
            let service = TowerToHyperService::new(router);
            if let Err(e) = auto::Builder::new(TokioExecutor::new())
                .serve_connection(TokioIo::new(stream), service)
                .await
            {
                tracing::debug!("HTTPS connection from {peer}: {e}");
            }
        });
    }
}
