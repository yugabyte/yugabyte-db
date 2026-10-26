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

//! Checks sys-catalog rows dumped by `sys-catalog-tool dump-rows` against the Rust protobuf
//! runtime.
//!
//! Usage: `master-conformance <dump-rows file>...`
//!
//! For every row, the Rust parse-and-serialize must produce the bytes the C++ runtime produced
//! (the dump's last column). That is the property that keeps an upgrade or rollback safe when the
//! Rust master rewrites rows, including rows with fields it does not know. Each row is also
//! checked with unknown fields of every wire type appended. Exits non-zero on any mismatch.

use std::collections::HashSet;
use std::error::Error;
use std::process::ExitCode;

use master_catalog_entity::{parse_entity, reserialize_entity};
use protobuf::reflect::{MessageDescriptor, RuntimeFieldType};
use protobuf::{CodedOutputStream, Enum, UnknownFields};
use yb_proto::pb::master_types::SysRowEntryType;

fn from_hex(s: &str) -> Result<Vec<u8>, Box<dyn Error>> {
    if s.len() % 2 != 0 {
        return Err(format!("odd-length hex string of {} chars", s.len()).into());
    }
    (0..s.len())
        .step_by(2)
        .map(|i| Ok(u8::from_str_radix(&s[i..i + 2], 16)?))
        .collect()
}

/// Whether the message type contains a map field at any depth. Map entries have no defined wire
/// order in either runtime, so rows with maps are compared by content when bytes differ.
fn has_map_fields(descriptor: &MessageDescriptor, seen: &mut HashSet<String>) -> bool {
    if !seen.insert(descriptor.full_name().to_string()) {
        return false;
    }
    descriptor.fields().any(|f| match f.runtime_field_type() {
        RuntimeFieldType::Map(..) => true,
        RuntimeFieldType::Singular(t) | RuntimeFieldType::Repeated(t) => match t {
            protobuf::reflect::RuntimeType::Message(m) => has_map_fields(&m, seen),
            _ => false,
        },
    })
}

/// Unknown fields of each wire type, with field numbers no release uses.
fn unknown_tail() -> Vec<u8> {
    let mut fields = UnknownFields::new();
    fields.add_varint(9001, 150);
    fields.add_fixed64(9002, 0x0102_0304_0506_0708);
    fields.add_length_delimited(9003, b"newer-release".to_vec());
    fields.add_fixed32(9004, 7);
    let mut out = Vec::new();
    let mut os = CodedOutputStream::vec(&mut out);
    os.write_unknown_fields(&fields).unwrap();
    os.flush().unwrap();
    drop(os);
    out
}

#[derive(Default)]
struct Counts {
    rows: usize,
    exact: usize,
    map_order_only: usize,
    failures: usize,
}

fn check_row(
    entry_type: SysRowEntryType,
    row: &[u8],
    cpp: &[u8],
    counts: &mut Counts,
) -> Result<(), String> {
    counts.rows += 1;
    let rust = reserialize_entity(entry_type, row).map_err(|e| e.to_string())?;
    if rust == cpp {
        counts.exact += 1;
    } else {
        let rust_msg = parse_entity(entry_type, &rust).map_err(|e| e.to_string())?;
        let cpp_msg = parse_entity(entry_type, cpp).map_err(|e| e.to_string())?;
        let descriptor = rust_msg.descriptor_dyn();
        if !has_map_fields(&descriptor, &mut HashSet::new()) {
            return Err("bytes differ from the C++ re-serialization".into());
        }
        if !descriptor.eq(&*rust_msg, &*cpp_msg) {
            return Err("content differs from the C++ re-serialization".into());
        }
        counts.map_order_only += 1;
    }

    // C++ writes unknown fields after the known ones, in the order it read them.
    let tail = unknown_tail();
    let mut with_unknown = row.to_vec();
    with_unknown.extend_from_slice(&tail);
    let rust = reserialize_entity(entry_type, &with_unknown).map_err(|e| e.to_string())?;
    if !rust.ends_with(&tail) {
        return Err("unknown fields were not preserved at the end of the row".into());
    }
    Ok(())
}

fn check_file(path: &str, counts: &mut Counts) -> Result<(), Box<dyn Error>> {
    let content = std::fs::read_to_string(path)?;
    for (line_no, line) in content.lines().enumerate() {
        let cols: Vec<&str> = line.split('\t').collect();
        let [type_name, id, row, cpp] = cols.as_slice() else {
            return Err(format!("{path}:{}: expected 4 columns", line_no + 1).into());
        };
        let entry_type = SysRowEntryType::from_str(type_name)
            .ok_or_else(|| format!("{path}:{}: unknown entry type {type_name}", line_no + 1))?;
        let (row, cpp) = (from_hex(row)?, from_hex(cpp)?);
        if let Err(e) = check_row(entry_type, &row, &cpp, counts) {
            counts.failures += 1;
            eprintln!("{path}:{}: {type_name} id={id}: {e}", line_no + 1);
        }
    }
    Ok(())
}

fn main() -> ExitCode {
    let paths: Vec<String> = std::env::args().skip(1).collect();
    if paths.is_empty() {
        eprintln!("usage: master-conformance <dump-rows file>...");
        return ExitCode::from(2);
    }
    let mut counts = Counts::default();
    for path in &paths {
        if let Err(e) = check_file(path, &mut counts) {
            eprintln!("{path}: {e}");
            return ExitCode::from(2);
        }
    }
    println!(
        "rows={} exact={} map_order_only={} failures={}",
        counts.rows, counts.exact, counts.map_order_only, counts.failures
    );
    if counts.failures == 0 && counts.rows > 0 {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

#[cfg(test)]
mod tests {
    use protobuf::{Message, MessageFull};
    use yb_proto::pb::catalog_entity_info::SysClusterConfigEntryPB;

    use super::*;

    #[test]
    fn hex() {
        assert_eq!(from_hex("00ff10").unwrap(), vec![0, 255, 16]);
        assert!(from_hex("abc").is_err());
    }

    #[test]
    fn cluster_config_has_maps() {
        // The xCluster consumer registry inside the cluster config uses map fields.
        let d = SysClusterConfigEntryPB::descriptor();
        assert!(has_map_fields(&d, &mut HashSet::new()));
    }

    #[test]
    fn row_checks() {
        let mut config = SysClusterConfigEntryPB::new();
        config.set_version(3);
        config.set_cluster_uuid("cluster-1".into());
        let bytes = config.write_to_bytes().unwrap();
        let mut counts = Counts::default();
        check_row(SysRowEntryType::CLUSTER_CONFIG, &bytes, &bytes, &mut counts).unwrap();
        assert_eq!(counts.exact, 1);

        let mut other = config.clone();
        other.set_version(4);
        let other = other.write_to_bytes().unwrap();
        let err = check_row(SysRowEntryType::CLUSTER_CONFIG, &bytes, &other, &mut counts);
        assert!(err.is_err());
    }
}
