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

//! A route that declines a request, so the host's C++ web server answers it.

use std::sync::Arc;

use axum::body::Body;
use axum::extract::{Request, State};
use axum::middleware::Next;
use axum::response::Response;

use crate::proxy::LoopbackProxy;

/// A response extension: the route does not serve this request (on a master, because it is not
/// the leader), and the request goes to the host's web server as if no route matched.
#[derive(Clone, Copy, Debug)]
pub struct PassToHost;

/// Middleware over the routes: forwards a request whose route answered [`PassToHost`]. Only
/// bodiless requests are forwarded again; the routes serve GETs.
pub(crate) async fn forward_passed(
    State(proxy): State<Arc<LoopbackProxy>>,
    request: Request,
    next: Next,
) -> Response {
    let (parts, body) = request.into_parts();
    let copy = Request::from_parts(parts.clone(), Body::empty());
    let response = next.run(Request::from_parts(parts, body)).await;
    if response.extensions().get::<PassToHost>().is_some() {
        proxy.forward(copy).await
    } else {
        response
    }
}
