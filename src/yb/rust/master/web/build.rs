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

//! Embeds the built web UI. `YB_MASTER_UI_DIR` names the output of `vite build` (`yb_build.sh
//! --build-master-ui` builds it, as `--build-yugabyted-ui` builds yugabyted's); without it the
//! binary has no UI and the pages say so.

use std::fmt::Write;
use std::path::{Path, PathBuf};

fn walk(dir: &Path, files: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.is_dir() {
            walk(&path, files);
        } else {
            files.push(path);
        }
    }
}

fn main() {
    println!("cargo:rerun-if-env-changed=YB_MASTER_UI_DIR");
    let mut code = String::from("pub(crate) static FILES: &[(&str, &[u8])] = &[\n");
    if let Some(dir) = std::env::var_os("YB_MASTER_UI_DIR").filter(|d| !d.is_empty()) {
        let dir = PathBuf::from(dir);
        println!("cargo:rerun-if-changed={}", dir.display());
        let mut files = Vec::new();
        walk(&dir, &mut files);
        files.sort();
        for file in files {
            println!("cargo:rerun-if-changed={}", file.display());
            let relative = file
                .strip_prefix(&dir)
                .expect("walked under dir")
                .to_string_lossy()
                .replace('\\', "/");
            let absolute = file.canonicalize().expect("a walked file exists");
            writeln!(code, "    ({relative:?}, include_bytes!({absolute:?})),")
                .expect("writing to a String");
        }
    }
    code.push_str("];\n");
    let out = PathBuf::from(std::env::var_os("OUT_DIR").expect("cargo sets OUT_DIR"));
    std::fs::write(out.join("ui_assets.rs"), code).expect("writing ui_assets.rs");
}
