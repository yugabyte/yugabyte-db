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

//! Generates the `SysRowEntryType` to message-type table from `CATALOG_ENTITY_TYPE_MAP` in
//! src/yb/master/catalog_entity_types.h, so it cannot drift from the C++ master.

use std::fmt::Write as _;
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let header_path = manifest_dir.join("../../../yb/master/catalog_entity_types.h");
    println!("cargo:rerun-if-changed={}", header_path.display());
    let header = std::fs::read_to_string(&header_path).unwrap();
    let start = header
        .find("#define CATALOG_ENTITY_TYPE_MAP")
        .expect("CATALOG_ENTITY_TYPE_MAP not found");

    let mut code = String::from(
        "/// `(SysRowEntryType, message type)` for every entry, from catalog_entity_types.h.\n\
         pub static CATALOG_ENTITY_TYPES: &[(SysRowEntryType, &str)] = &[\n",
    );
    let mut count = 0;
    for line in header[start..].lines().skip(1) {
        let Some(inner) = line.trim().strip_prefix("((") else {
            break;
        };
        let inner = &inner[..inner.find("))").unwrap()];
        let (entry_type, pb) = inner.split_once(',').unwrap();
        writeln!(
            code,
            "    (SysRowEntryType::{}, \"yb.master.{}\"),",
            entry_type.trim(),
            pb.trim()
        )
        .unwrap();
        count += 1;
    }
    assert!(count > 0, "no entries parsed from CATALOG_ENTITY_TYPE_MAP");
    code.push_str("];\n");
    let out = PathBuf::from(std::env::var("OUT_DIR").unwrap()).join("catalog_entity_types.rs");
    std::fs::write(out, code).unwrap();
}
