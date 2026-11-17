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

//! The web UI: one page app (`ui/`, built by `yb_build.sh --build-master-ui`) served at every old
//! page URL, reading the JSON pages. Its files are under `/ui/`.

use std::sync::Arc;

use axum::body::Body;
use axum::extract::Request;
use axum::http::{header, HeaderValue, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::get;
use axum::Router;

use crate::{pass_to_host, MasterWeb};

mod embedded {
    include!(concat!(env!("OUT_DIR"), "/ui_assets.rs"));
}

/// The HTML pages of `MasterPathHandlers::Register` the UI replaces.
pub const PAGES: &[&str] = &[
    "/",
    "/tablet-servers",
    "/tablet-server-clocks",
    "/tables",
    "/namespaces",
    "/table",
    "/cluster-config",
    "/xcluster",
    "/tasks",
    "/tablet-replication",
    "/load-distribution",
    "/stateful-services",
];

/// The built UI's files by path, `index.html` among them.
#[derive(Clone, Copy)]
pub struct UiAssets {
    files: &'static [(&'static str, &'static [u8])],
}

impl UiAssets {
    /// The UI built into this binary, if any.
    pub fn embedded() -> Option<UiAssets> {
        UiAssets::new(embedded::FILES)
    }

    /// `files` as a UI, if it has an `index.html`.
    pub fn new(files: &'static [(&'static str, &'static [u8])]) -> Option<UiAssets> {
        files
            .iter()
            .any(|(path, _)| *path == "index.html")
            .then_some(UiAssets { files })
    }

    fn get(&self, path: &str) -> Option<&'static [u8]> {
        self.files
            .iter()
            .find(|(p, _)| *p == path)
            .map(|(_, bytes)| *bytes)
    }
}

fn content_type(path: &str) -> &'static str {
    match path.rsplit_once('.').map(|(_, ext)| ext) {
        Some("html") => "text/html; charset=utf-8",
        Some("js") | Some("mjs") => "text/javascript; charset=utf-8",
        Some("css") => "text/css; charset=utf-8",
        Some("json") | Some("map") => "application/json",
        Some("svg") => "image/svg+xml",
        Some("png") => "image/png",
        Some("ico") => "image/x-icon",
        Some("woff2") => "font/woff2",
        Some("woff") => "font/woff",
        Some("txt") => "text/plain; charset=utf-8",
        _ => "application/octet-stream",
    }
}

fn file_response(path: &str, bytes: &'static [u8], immutable: bool) -> Response {
    let mut response = Response::new(Body::from(bytes));
    let headers = response.headers_mut();
    headers.insert(
        header::CONTENT_TYPE,
        HeaderValue::from_static(content_type(path)),
    );
    headers.insert(
        header::X_CONTENT_TYPE_OPTIONS,
        HeaderValue::from_static("nosniff"),
    );
    // Vite names assets by content hash; the page itself must be refetched to pick up a new build.
    headers.insert(
        header::CACHE_CONTROL,
        HeaderValue::from_static(if immutable {
            "public, max-age=31536000, immutable"
        } else {
            "no-cache"
        }),
    );
    response
}

/// Every master serves the UI for the page URLs: a follower's JSON is the leader's, fetched by
/// the host. Without a built UI a leader says how to build it and a follower lets the host fetch
/// the leader's page.
async fn page(web: Arc<MasterWeb>) -> Response {
    match web.ui {
        Some(ui) => match ui.get("index.html") {
            Some(index) => file_response("index.html", index, false),
            None => StatusCode::NOT_FOUND.into_response(),
        },
        None if web.leader.ready().is_ok() => no_ui_page(),
        None => pass_to_host(),
    }
}

async fn asset(web: Arc<MasterWeb>, request: Request) -> Response {
    let path = request
        .uri()
        .path()
        .strip_prefix("/ui/")
        .unwrap_or_default()
        .to_string();
    match web.ui.and_then(|ui| ui.get(&path)) {
        Some(bytes) => file_response(&path, bytes, path.starts_with("assets/")),
        None => StatusCode::NOT_FOUND.into_response(),
    }
}

fn no_ui_page() -> Response {
    let mut body = String::from(
        "<!DOCTYPE html><html><head><title>YugabyteDB master</title></head><body>\
         <h2>This yb-master was built without its web UI.</h2>\
         <p>Build it with <code>yb_build.sh --build-master-ui</code>. The JSON pages are \
         served:</p><ul>",
    );
    for (path, _) in crate::LEADER_JSON {
        body.push_str(&format!("<li><a href=\"{path}\">{path}</a></li>"));
    }
    body.push_str("</ul></body></html>");
    let mut response = Response::new(Body::from(body));
    response.headers_mut().insert(
        header::CONTENT_TYPE,
        HeaderValue::from_static("text/html; charset=utf-8"),
    );
    response
}

pub(crate) fn routes(mut router: Router, web: Arc<MasterWeb>) -> Router {
    for path in PAGES {
        let web = web.clone();
        router = router.route(path, get(move || page(web.clone())));
    }
    router.route(
        "/ui/{*path}",
        get(move |request: Request| asset(web.clone(), request)),
    )
}
