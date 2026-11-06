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

use std::net::ToSocketAddrs;
use std::pin::Pin;

use axum::routing::get;
use openssl::asn1::Asn1Time;
use openssl::hash::MessageDigest;
use openssl::pkey::PKey;
use openssl::rsa::Rsa;
use openssl::ssl::{SslConnector, SslMethod, SslVerifyMode};
use openssl::x509::X509;
use tokio::io::{AsyncRead, AsyncReadExt, AsyncWrite, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio_openssl::SslStream;

use super::*;

/// Stands in for the host's C++ web server: answers every request with its request line.
async fn fake_loopback_server() -> u16 {
    let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
    let port = listener.local_addr().unwrap().port();
    tokio::spawn(async move {
        loop {
            let (mut socket, _) = listener.accept().await.unwrap();
            tokio::spawn(async move {
                let mut buf = vec![0; 4096];
                let n = socket.read(&mut buf).await.unwrap();
                let request = String::from_utf8_lossy(&buf[..n]).to_string();
                let path = request.split_whitespace().nth(1).unwrap_or("").to_string();
                let body = format!("cpp page {path}");
                let response = format!(
                    "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nContent-Type: text/plain\r\n\
                     Connection: close\r\n\r\n{body}",
                    body.len()
                );
                socket.write_all(response.as_bytes()).await.unwrap();
            });
        }
    });
    port
}

async fn start_server(tls: Option<&TlsSettings>) -> SocketAddr {
    let loopback_port = fake_loopback_server().await;
    let listener = WebListener::bind("127.0.0.1", 0, tls).unwrap();
    let addr = listener.local_addrs()[0];
    let routes = Router::new().route("/rust", get(|| async { "rust page" }));
    tokio::spawn(listener.serve(routes, loopback_port));
    addr
}

/// The whole response, head and body.
async fn raw_get<S: AsyncRead + AsyncWrite + Unpin>(mut stream: S, path: &str) -> String {
    let request = format!("GET {path} HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    stream.write_all(request.as_bytes()).await.unwrap();
    let mut response = String::new();
    // An HTTPS server may close without close_notify; what was read is the whole response.
    let _ = stream.read_to_string(&mut response).await;
    response
}

fn status_and_body(response: &str) -> (u16, String) {
    let status: u16 = response.split_whitespace().nth(1).unwrap().parse().unwrap();
    let body = response.split("\r\n\r\n").nth(1).unwrap_or("").to_string();
    (status, body)
}

async fn http_get(addr: SocketAddr, path: &str) -> (u16, String) {
    status_and_body(&raw_get(TcpStream::connect(addr).await.unwrap(), path).await)
}

#[tokio::test]
async fn rust_routes_are_served_and_the_rest_proxied() {
    let addr = start_server(None).await;
    assert_eq!(
        http_get(addr, "/rust").await,
        (200, "rust page".to_string())
    );
    assert_eq!(
        http_get(addr, "/varz?raw").await,
        (200, "cpp page /varz?raw".to_string())
    );
    assert_eq!(
        http_get(addr, "/prometheus-metrics").await,
        (200, "cpp page /prometheus-metrics".to_string())
    );
}

#[tokio::test]
async fn unreachable_loopback_server_is_a_bad_gateway() {
    let listener = WebListener::bind("127.0.0.1", 0, None).unwrap();
    let addr = listener.local_addrs()[0];
    // Nothing listens on port 1.
    tokio::spawn(listener.serve(Router::new(), 1));
    assert_eq!(http_get(addr, "/varz").await.0, 502);
}

/// A self-signed certificate and its key, as PEM files in a fresh directory.
fn write_certificate(dir: &std::path::Path) -> TlsSettings {
    let key = PKey::from_rsa(Rsa::generate(2048).unwrap()).unwrap();
    let mut name = openssl::x509::X509NameBuilder::new().unwrap();
    name.append_entry_by_text("CN", "127.0.0.1").unwrap();
    let name = name.build();
    let mut cert = X509::builder().unwrap();
    cert.set_version(2).unwrap();
    cert.set_subject_name(&name).unwrap();
    cert.set_issuer_name(&name).unwrap();
    cert.set_pubkey(&key).unwrap();
    cert.set_not_before(&Asn1Time::days_from_now(0).unwrap())
        .unwrap();
    cert.set_not_after(&Asn1Time::days_from_now(1).unwrap())
        .unwrap();
    cert.sign(&key, MessageDigest::sha256()).unwrap();
    let cert = cert.build();
    let certificate_file = dir.join("node.crt");
    let private_key_file = dir.join("node.key");
    std::fs::write(&certificate_file, cert.to_pem().unwrap()).unwrap();
    std::fs::write(&private_key_file, key.private_key_to_pem_pkcs8().unwrap()).unwrap();
    TlsSettings {
        certificate_file: certificate_file.to_string_lossy().into_owned(),
        private_key_file: private_key_file.to_string_lossy().into_owned(),
        strict_transport_security: Some(|| true),
        ..Default::default()
    }
}

fn temp_dir(name: &str) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("yb-web-{name}-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

#[tokio::test]
async fn serves_https() {
    let dir = temp_dir("https");
    let settings = write_certificate(&dir);
    let addr = start_server(Some(&settings)).await;

    let mut connector = SslConnector::builder(SslMethod::tls()).unwrap();
    connector.set_verify(SslVerifyMode::NONE);
    let ssl = connector
        .build()
        .configure()
        .unwrap()
        .into_ssl("127.0.0.1")
        .unwrap();
    let mut stream = SslStream::new(ssl, TcpStream::connect(addr).await.unwrap()).unwrap();
    Pin::new(&mut stream).connect().await.unwrap();
    let response = raw_get(stream, "/varz").await;
    assert_eq!(
        status_and_body(&response),
        (200, "cpp page /varz".to_string())
    );
    assert!(
        response.contains("strict-transport-security: max-age=31536000\r\n"),
        "{response}"
    );

    // Plain HTTP on the HTTPS port gets no page.
    let mut plain = TcpStream::connect(addr).await.unwrap();
    plain
        .write_all(b"GET /varz HTTP/1.1\r\nHost: localhost\r\n\r\n")
        .await
        .unwrap();
    let mut response = String::new();
    let _ = plain.read_to_string(&mut response).await;
    assert!(!response.contains("cpp page"), "{response}");
    std::fs::remove_dir_all(dir).unwrap();
}

#[test]
fn bad_tls_settings_fail_bind() {
    let result = WebListener::bind(
        "127.0.0.1",
        0,
        Some(&TlsSettings {
            certificate_file: "/nonexistent/cert.pem".into(),
            ..Default::default()
        }),
    );
    assert_eq!(
        result.err().map(|e| e.code),
        Some(StatusCode::ConfigurationError)
    );
}

#[tokio::test]
async fn binds_every_address_of_the_host() {
    let listener = WebListener::bind("localhost", 0, None).unwrap();
    let addrs = listener.local_addrs();
    let mut expected: Vec<_> = ("localhost", 0)
        .to_socket_addrs()
        .unwrap()
        .map(|a| a.ip())
        .collect();
    expected.dedup();
    assert_eq!(addrs.iter().map(|a| a.ip()).collect::<Vec<_>>(), expected);
    assert!(addrs.iter().all(|a| a.port() == addrs[0].port()));
    let loopback_port = fake_loopback_server().await;
    tokio::spawn(listener.serve(Router::new(), loopback_port));
    for addr in addrs {
        assert_eq!(http_get(addr, "/x").await, (200, "cpp page /x".to_string()));
    }
}
