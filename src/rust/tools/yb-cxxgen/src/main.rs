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

//! Writes the C++ half of `#[cxx::bridge]` modules so that CMake compiles it with the same
//! compiler, flags and standard library as the rest of the server, instead of `cxx-build`
//! compiling it from a build script.
//!
//! Usage: `yb-cxxgen <src/rust dir> <output dir> <bridge.rs relative path>...`
//!
//! For each `crate/src/x.rs` it writes `<output dir>/crate/src/x.rs.h` and `.rs.cc`, plus the cxx
//! runtime header as `<output dir>/rust/cxx.h`. Files are only rewritten when their content
//! changes, so an unrelated edit to a bridge file does not recompile its C++ dependents.

use std::error::Error;
use std::fs;
use std::path::{Path, PathBuf};
use std::str::FromStr;

fn write_if_changed(path: &Path, content: &[u8]) -> Result<(), Box<dyn Error>> {
    if fs::read(path).ok().as_deref() == Some(content) {
        return Ok(());
    }
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    fs::write(path, content)?;
    Ok(())
}

fn with_suffix(base: &Path, rel: &str, suffix: &str) -> PathBuf {
    base.join(format!("{rel}{suffix}"))
}

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let [src_root, out_dir, bridges @ ..] = args.as_slice() else {
        return Err("usage: yb-cxxgen <src/rust dir> <output dir> <bridge.rs>...".into());
    };
    let src_root = Path::new(src_root);
    let out_dir = Path::new(out_dir);

    write_if_changed(&out_dir.join("rust/cxx.h"), cxx_gen::HEADER.as_bytes())?;
    for rel in bridges {
        let source =
            fs::read_to_string(src_root.join(rel)).map_err(|e| format!("reading {rel}: {e}"))?;
        let tokens = proc_macro2::TokenStream::from_str(&source)
            .map_err(|e| format!("tokenizing {rel}: {e}"))?;
        let generated = cxx_gen::generate_header_and_cc(tokens, &cxx_gen::Opt::default())
            .map_err(|e| format!("generating C++ for {rel}: {e}"))?;
        write_if_changed(&with_suffix(out_dir, rel, ".h"), &generated.header)?;
        write_if_changed(&with_suffix(out_dir, rel, ".cc"), &generated.implementation)?;
    }
    Ok(())
}
