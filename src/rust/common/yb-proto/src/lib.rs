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

//! Rust types for YugabyteDB's protobufs, generated at build time from `src/yb/**/*.proto` with
//! rust-protobuf 3, plus a table of the RPC services they declare. See docs/proto.md for why that
//! crate and how its output is made to match the C++ runtime byte for byte.
//!
//! rust-protobuf keeps unknown fields and re-serializes them, which an upgrade relies on: an older
//! server may rewrite persisted messages that carry fields only a newer release knows. Text format
//! comes from the C++ protobuf runtime in the host process, not from this crate.

/// Generated messages, one module per `.proto` file, named after the file. The two files named
/// `metadata.proto` are `consensus_metadata` and `tablet_metadata` here.
#[allow(missing_docs, clippy::all)]
pub mod pb {
    include!(concat!(env!("OUT_DIR"), "/pb/mod.rs"));
}

mod services;

pub use services::{find_method, MethodDesc, ServiceDesc, SERVICES};

include!(concat!(env!("OUT_DIR"), "/files.rs"));

/// Finds a generated message type by full name, e.g. `yb.master.SysTablesEntryPB`.
pub fn message_descriptor(full_name: &str) -> Option<protobuf::reflect::MessageDescriptor> {
    file_descriptors()
        .iter()
        .find_map(|f| f.message_by_full_name(&format!(".{full_name}")))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn message_lookup() {
        let d = message_descriptor("yb.master.SysTabletsEntryPB").unwrap();
        assert_eq!(d.name(), "SysTabletsEntryPB");
        assert!(message_descriptor("yb.master.NoSuchMessagePB").is_none());
    }
}
