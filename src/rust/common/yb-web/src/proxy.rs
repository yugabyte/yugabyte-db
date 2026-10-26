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

//! A reverse proxy to the host's C++ web server on 127.0.0.1.

use axum::body::Body;
use axum::extract::Request;
use axum::http::{header, StatusCode, Uri};
use axum::response::{IntoResponse, Response};
use hyper_util::client::legacy::connect::HttpConnector;
use hyper_util::client::legacy::Client;
use hyper_util::rt::TokioExecutor;

pub(crate) struct LoopbackProxy {
    port: u16,
    client: Client<HttpConnector, Body>,
}

impl LoopbackProxy {
    pub(crate) fn new(port: u16) -> LoopbackProxy {
        LoopbackProxy {
            port,
            client: Client::builder(TokioExecutor::new()).build_http(),
        }
    }

    pub(crate) async fn forward(&self, mut request: Request) -> Response {
        let path_and_query = request
            .uri()
            .path_and_query()
            .map(|pq| pq.as_str().to_string())
            .unwrap_or_else(|| "/".to_string());
        let uri: Uri = match format!("http://127.0.0.1:{}{path_and_query}", self.port).parse() {
            Ok(uri) => uri,
            Err(e) => return (StatusCode::BAD_REQUEST, e.to_string()).into_response(),
        };
        *request.uri_mut() = uri;
        // Host stays the browser's (the client only adds one when missing), so the C++ server's
        // absolute links point back at this port. Connection is hop-by-hop.
        request.headers_mut().remove(header::CONNECTION);
        match self.client.request(request).await {
            Ok(response) => response.map(Body::new),
            Err(e) => (
                StatusCode::BAD_GATEWAY,
                format!("The master's internal web server did not answer: {e}"),
            )
                .into_response(),
        }
    }
}
