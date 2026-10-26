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

//! The master's web port. The pages themselves are the C++ host's (`MasterHost::RegisterWebPages`);
//! Rust serves the port and gives the host the master list.

use std::sync::Arc;
use std::time::Duration;

use master_rpc_handlers::{list_masters_response, MasterInfo};
use protobuf::Message;
use yb_flags::FlagRef;
use yb_host_api::rpc::RpcClient;
use yb_proto::pb::master_cluster::ListMastersResponsePB;
use yb_proto::pb::master_types::{master_error_pb, MasterErrorPB};
use yb_proto::pb::wire_protocol::{app_status_pb, AppStatusPB};
use yb_web::{TlsSettings, WebListener};

use crate::ffi::bridge::MasterHost;

static WEBSERVER_STRICT_TRANSPORT_SECURITY: FlagRef<bool> =
    FlagRef::new("webserver_strict_transport_security");

pub(crate) struct WebMasterList {
    pub(crate) info: Arc<dyn MasterInfo>,
    pub(crate) rpc: Arc<dyn RpcClient>,
    pub(crate) runtime: tokio::runtime::Handle,
    pub(crate) master_rpc_timeout: fn() -> Duration,
}

impl WebMasterList {
    pub(crate) fn list_masters(&self) -> Vec<u8> {
        let resp = self.runtime.block_on(list_masters_response(
            self.info.as_ref(),
            self.rpc.clone(),
            (self.master_rpc_timeout)(),
        ));
        resp.write_to_bytes().unwrap_or_else(|e| {
            let mut status = AppStatusPB::new();
            status.set_code(app_status_pb::ErrorCode::INTERNAL_ERROR);
            status.set_message(format!("Serializing the master list: {e}"));
            let mut error = MasterErrorPB::new();
            error.set_code(master_error_pb::Code::UNKNOWN_ERROR);
            error.status = Some(status).into();
            let mut resp = ListMastersResponsePB::new();
            resp.error = Some(error).into();
            resp.write_to_bytes()
                .expect("the error response is complete")
        })
    }
}

/// Binds the web port configured by the webserver flags.
pub(crate) fn bind(host: &MasterHost) -> Result<WebListener, String> {
    let certificate_file = host.web_certificate_file();
    let tls = (!certificate_file.is_empty()).then(|| TlsSettings {
        certificate_file,
        private_key_file: host.web_private_key_file(),
        private_key_password: host.web_private_key_password(),
        ciphers: host.web_ssl_ciphers(),
        strict_transport_security: Some(|| WEBSERVER_STRICT_TRANSPORT_SECURITY.get()),
    });
    WebListener::bind(
        &host.public_web_interface(),
        host.public_web_port(),
        tls.as_ref(),
    )
    .map_err(|e| e.to_string())
}
