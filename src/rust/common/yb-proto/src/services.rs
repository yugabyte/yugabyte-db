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

//! RPC services, generated from the service declarations in the root `.proto` files.

/// One RPC service.
#[derive(Debug)]
pub struct ServiceDesc {
    /// The proto name, e.g. `yb.master.MasterCluster`.
    pub proto_name: &'static str,
    /// The service name on the wire: `custom_service_name` when set, else the proto name. Several
    /// services can share one, e.g. every master service except MasterBackup uses
    /// `yb.master.MasterService`.
    pub wire_name: &'static str,
    /// Methods in declaration order; the index is the method id used by the RPC bridge.
    pub methods: &'static [MethodDesc],
}

/// One RPC method.
#[derive(Debug)]
pub struct MethodDesc {
    /// The method name, e.g. `GetMasterRegistration`.
    pub name: &'static str,
    /// Full name of the request message.
    pub request_type: &'static str,
    /// Full name of the response message.
    pub response_type: &'static str,
    /// Number and message type of the response's `error` field, when it has one (`MasterErrorPB`
    /// on master responses). Lets a server answer with an error without knowing the response type.
    pub error_field: Option<(i32, &'static str)>,
    /// Whether the response has `required` fields of its own. Such a response cannot be answered
    /// with only `error` set: the C++ runtime rejects a message with a required field missing.
    pub response_has_required_fields: bool,
}

include!(concat!(env!("OUT_DIR"), "/services.rs"));

/// Finds a method by wire service name and method name.
pub fn find_method(
    wire_name: &str,
    method: &str,
) -> Option<(&'static ServiceDesc, usize, &'static MethodDesc)> {
    SERVICES
        .iter()
        .filter(|s| s.wire_name == wire_name)
        .find_map(|s| {
            s.methods
                .iter()
                .enumerate()
                .find(|(_, m)| m.name == method)
                .map(|(i, m)| (s, i, m))
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wire_names() {
        let backup = SERVICES
            .iter()
            .find(|s| s.proto_name == "yb.master.MasterBackup")
            .unwrap();
        assert_eq!(backup.wire_name, "yb.master.MasterBackup");
        for s in SERVICES
            .iter()
            .filter(|s| s.proto_name != "yb.master.MasterBackup")
        {
            assert_eq!(s.wire_name, "yb.master.MasterService", "{}", s.proto_name);
        }
    }

    #[test]
    fn method_lookup() {
        let (service, _, method) =
            find_method("yb.master.MasterService", "GetMasterRegistration").unwrap();
        assert_eq!(service.proto_name, "yb.master.MasterCluster");
        assert_eq!(
            method.request_type,
            "yb.master.GetMasterRegistrationRequestPB"
        );
        assert_eq!(method.error_field, Some((4, "yb.master.MasterErrorPB")));
        assert!(method.response_has_required_fields);
    }

    #[test]
    fn method_names_unique_per_wire_name() {
        let mut seen = std::collections::HashSet::new();
        for s in SERVICES {
            for m in s.methods {
                assert!(
                    seen.insert((s.wire_name, m.name)),
                    "{}.{}",
                    s.wire_name,
                    m.name
                );
            }
        }
    }
}
