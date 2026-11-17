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

//! HTTP Digest authentication against an htdigest file, as squeasel (the C++ web server's HTTP
//! library) does with its global passwords file: same header parsing, same response check, same
//! challenge. Like squeasel, the nonce is the current time and is not checked, and neither is the
//! `uri` parameter against the request.

use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

use axum::extract::{Request, State};
use axum::http::{header, HeaderValue, StatusCode as HttpStatus};
use axum::middleware::Next;
use axum::response::{IntoResponse, Response};
use openssl::hash::{hash, MessageDigest};
use yb_host_api::{Status, StatusCode};

/// `webserver_password_file` and `webserver_authentication_domain`.
#[derive(Clone, Debug, Default)]
pub struct AuthSettings {
    /// An htdigest file: `user:realm:md5(user:realm:password)` lines. Read on every request, as
    /// squeasel does, so edits apply without a restart.
    pub password_file: String,
    /// The realm; squeasel's `mydomain.com` when empty.
    pub domain: String,
}

const DEFAULT_DOMAIN: &str = "mydomain.com";

pub(crate) struct DigestAuth {
    password_file: PathBuf,
    realm: String,
}

impl DigestAuth {
    pub(crate) fn new(settings: &AuthSettings) -> Result<DigestAuth, Status> {
        if !Path::new(&settings.password_file).exists() {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                format!(
                    "Webserver: Password file does not exist: {}",
                    settings.password_file
                ),
            ));
        }
        let realm = if settings.domain.is_empty() {
            DEFAULT_DOMAIN
        } else {
            &settings.domain
        };
        let auth = DigestAuth {
            password_file: settings.password_file.clone().into(),
            realm: realm.to_string(),
        };
        // Fail at startup rather than on every request if the realm cannot be a header value.
        auth.challenge_header().map_err(|e| {
            Status::new(
                StatusCode::InvalidArgument,
                format!("webserver_authentication_domain {realm:?}: {e}"),
            )
        })?;
        Ok(auth)
    }

    fn challenge_header(&self) -> Result<HeaderValue, header::InvalidHeaderValue> {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_or(0, |d| d.as_secs());
        HeaderValue::from_str(&format!(
            "Digest qop=\"auth\", realm=\"{}\", nonce=\"{nonce}\"",
            self.realm
        ))
    }

    /// squeasel's `authorize`: the first line for the user in this realm decides.
    fn authorize(&self, credentials: &Credentials, method: &str, password_file: &str) -> bool {
        for line in password_file.split('\n') {
            // sscanf("%[^:]:%[^:]:%s"): user and realm non-empty, ha1 the next word.
            let mut fields = line.splitn(3, ':');
            let (Some(user), Some(realm), Some(rest)) =
                (fields.next(), fields.next(), fields.next())
            else {
                continue;
            };
            let Some(ha1) = rest.split_ascii_whitespace().next() else {
                continue;
            };
            if user.is_empty() || realm.is_empty() {
                continue;
            }
            if user == credentials.user && realm == self.realm {
                return credentials.check(method, ha1);
            }
        }
        false
    }
}

/// The parameters of a `Digest` Authorization header that the check uses.
#[derive(Default)]
struct Credentials {
    user: String,
    uri: Option<String>,
    cnonce: Option<String>,
    response: Option<String>,
    qop: Option<String>,
    nc: Option<String>,
    nonce: Option<String>,
}

/// squeasel's `skip_quoted`: the text up to the first delimiter, and the rest after it and any
/// following `whitespace`. With `escape`, an escaped delimiter is part of the text.
fn skip_quoted<'a>(
    s: &'a str,
    delimiters: &[char],
    whitespace: char,
    escape: Option<char>,
) -> (String, &'a str) {
    let mut word = String::new();
    let mut rest = s;
    loop {
        let Some(i) = rest.find(delimiters) else {
            word.push_str(rest);
            if escape.is_some_and(|e| word.ends_with(e)) {
                word.pop();
            }
            return (word, "");
        };
        word.push_str(&rest[..i]);
        // Delimiters are ASCII.
        let delimiter = &rest[i..i + 1];
        rest = &rest[i + 1..];
        if escape.is_some_and(|e| word.ends_with(e)) {
            word.pop();
            word.push_str(delimiter);
            continue;
        }
        return (word, rest.trim_start_matches(whitespace));
    }
}

impl Credentials {
    /// squeasel's `parse_auth_header`; None unless it is a Digest header with a username.
    fn parse(header: &str) -> Option<Credentials> {
        let mut s = header
            .get(..7)
            .filter(|scheme| scheme.eq_ignore_ascii_case("Digest "))
            .map(|_| &header[7..])?;
        let mut credentials = Credentials::default();
        let mut user = None;
        loop {
            s = s.trim_start_matches(|c: char| c.is_ascii_whitespace() || c == '\x0b');
            let (name, rest) = skip_quoted(s, &['='], ' ', None);
            s = rest;
            // A value is quoted, or ends at a comma or a space (IE uses commas, Firefox spaces).
            let value = if let Some(quoted) = s.strip_prefix('"') {
                let (value, rest) = skip_quoted(quoted, &['"'], ' ', Some('\\'));
                s = rest.strip_prefix(',').unwrap_or(rest);
                value
            } else {
                let (value, rest) = skip_quoted(s, &[',', ' '], ' ', None);
                s = rest;
                value
            };
            if name.is_empty() {
                break;
            }
            match name.as_str() {
                "username" => user = Some(value),
                "cnonce" => credentials.cnonce = Some(value),
                "response" => credentials.response = Some(value),
                "uri" => credentials.uri = Some(value),
                "qop" => credentials.qop = Some(value),
                "nc" => credentials.nc = Some(value),
                "nonce" => credentials.nonce = Some(value),
                _ => {}
            }
        }
        credentials.user = user?;
        Some(credentials)
    }

    /// squeasel's `check_password`: response = md5(ha1:nonce:nc:cnonce:qop:md5(method:uri)).
    fn check(&self, method: &str, ha1: &str) -> bool {
        let (Some(nonce), Some(nc), Some(cnonce), Some(qop), Some(response)) = (
            &self.nonce,
            &self.nc,
            &self.cnonce,
            &self.qop,
            &self.response,
        ) else {
            return false;
        };
        if response.len() != 32 {
            return false;
        }
        // squeasel hashes "method:" when the uri is missing.
        let uri = self.uri.as_deref().unwrap_or("");
        let Some(ha2) = md5_hex(&format!("{method}:{uri}")) else {
            return false;
        };
        let Some(expected) = md5_hex(&format!("{ha1}:{nonce}:{nc}:{cnonce}:{qop}:{ha2}")) else {
            return false;
        };
        openssl::memcmp::eq(
            response.to_ascii_lowercase().as_bytes(),
            expected.as_bytes(),
        )
    }
}

fn md5_hex(data: &str) -> Option<String> {
    let digest = hash(MessageDigest::md5(), data.as_bytes())
        .inspect_err(|e| tracing::warn!("MD5 for web authentication: {e}"))
        .ok()?;
    Some(digest.iter().map(|b| format!("{b:02x}")).collect())
}

/// Middleware: passes authorized requests on, answers the rest with a challenge.
pub(crate) async fn require_digest(
    State(auth): State<Arc<DigestAuth>>,
    request: Request,
    next: Next,
) -> Response {
    let credentials = request
        .headers()
        .get(header::AUTHORIZATION)
        .and_then(|value| Credentials::parse(&String::from_utf8_lossy(value.as_bytes())));
    let authorized = match credentials {
        None => false,
        Some(credentials) => {
            let path = auth.password_file.clone();
            match tokio::task::spawn_blocking(move || std::fs::read(path)).await {
                Ok(Ok(file)) => auth.authorize(
                    &credentials,
                    request.method().as_str(),
                    &String::from_utf8_lossy(&file),
                ),
                // squeasel lets everyone in when it cannot open the file; fail closed instead.
                Ok(Err(e)) => {
                    tracing::warn!(
                        "Reading the web password file {}: {e}",
                        auth.password_file.display()
                    );
                    false
                }
                Err(e) => {
                    tracing::warn!("Reading the web password file: {e}");
                    false
                }
            }
        }
    };
    if authorized {
        return next.run(request).await;
    }
    match auth.challenge_header() {
        Ok(challenge) => (
            HttpStatus::UNAUTHORIZED,
            [(header::WWW_AUTHENTICATE, challenge)],
        )
            .into_response(),
        // Checked at startup.
        Err(_) => HttpStatus::UNAUTHORIZED.into_response(),
    }
}
