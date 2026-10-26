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

//! Generates Rust code from the `.proto` files under `src/yb`, never from copies.
//!
//! Outputs in `OUT_DIR`:
//! - `pb/`: rust-protobuf messages for every `.proto` file in `ROOT_DIRS` and everything those
//!   import, transitively.
//! - `services.rs`: the wire name and methods of every RPC service in `ROOT_DIRS`.
//! - `files.rs`: the descriptors of every generated file, for lookups by message name.

use std::collections::{BTreeMap, BTreeSet};
use std::fmt::Write as _;
use std::fs;
use std::path::{Path, PathBuf};

use protobuf::descriptor::{field_descriptor_proto, DescriptorProto, FileDescriptorProto};
use protobuf::UnknownValueRef;

/// Directories (under src/) whose `.proto` files are generated, with their imports. A component's
/// directory is added when that component moves to Rust.
const ROOT_DIRS: &[&str] = &["yb/master"];

/// Field number of `custom_service_name` in yb/rpc/service.proto.
const CUSTOM_SERVICE_NAME_OPTION: u32 = 50011;

/// rust-protobuf names a module after the file's basename, so files that share one get distinct
/// names in the copy that is compiled. Only the file name changes; packages and the wire format
/// do not.
const RENAMES: &[(&str, &str)] = &[
    (
        "yb/consensus/metadata.proto",
        "yb/consensus/consensus_metadata.proto",
    ),
    (
        "yb/tablet/metadata.proto",
        "yb/tablet/tablet_metadata.proto",
    ),
];

fn renamed(path: &str) -> String {
    RENAMES
        .iter()
        .find(|(from, _)| *from == path)
        .map_or_else(|| path.to_string(), |(_, to)| to.to_string())
}

fn imports(source: &str) -> Vec<String> {
    source
        .lines()
        .filter_map(|line| {
            let rest = line.trim_start().strip_prefix("import")?.trim_start();
            let rest = rest.strip_prefix("public").unwrap_or(rest).trim_start();
            let rest = rest.strip_prefix('"')?;
            Some(rest[..rest.find('"')?].to_string())
        })
        .collect()
}

/// The `.proto` files in `ROOT_DIRS` and everything they import, except google/protobuf/*.
fn proto_closure(src_dir: &Path) -> BTreeSet<String> {
    let mut stack = Vec::new();
    for dir in ROOT_DIRS {
        for entry in fs::read_dir(src_dir.join(dir)).expect("listing a proto root directory") {
            let name = entry.unwrap().file_name().into_string().unwrap();
            if name.ends_with(".proto") {
                stack.push(format!("{dir}/{name}"));
            }
        }
    }
    let mut closure = BTreeSet::new();
    while let Some(path) = stack.pop() {
        if path.starts_with("google/protobuf/") || !closure.insert(path.clone()) {
            continue;
        }
        let source = fs::read_to_string(src_dir.join(&path))
            .unwrap_or_else(|e| panic!("reading {path}: {e}"));
        stack.extend(imports(&source));
    }
    closure
}

/// Copies the closure into `dir`, applying `RENAMES` to file names and import lines.
fn copy_renamed(src_dir: &Path, closure: &BTreeSet<String>, dir: &Path) -> Vec<PathBuf> {
    let mut inputs = Vec::new();
    for path in closure {
        println!("cargo:rerun-if-changed={}", src_dir.join(path).display());
        let mut source = fs::read_to_string(src_dir.join(path)).expect("reading proto");
        for (from, to) in RENAMES {
            source = source.replace(&format!("\"{from}\""), &format!("\"{to}\""));
        }
        let dest = dir.join(renamed(path));
        fs::create_dir_all(dest.parent().unwrap()).unwrap();
        fs::write(&dest, source).unwrap();
        inputs.push(dest);
    }
    inputs
}

/// rust-protobuf serializes fields in declaration order, but the C++ runtime serializes them in
/// field-number order. Rows the Rust master rewrites must be byte-identical to what a C++ master
/// would write (skip-if-unchanged writes and the master conformance tests compare bytes), so messages are
/// generated with their fields sorted by number. Oneof membership is by index, so it is unaffected.
fn sort_fields_by_number(messages: &mut [DescriptorProto]) {
    for m in messages {
        m.field.sort_by_key(|f| f.number());
        sort_fields_by_number(&mut m.nested_type);
    }
}

struct NoCustomization;

impl protobuf_codegen::CustomizeCallback for NoCustomization {}

fn module_name(proto_path: &str) -> String {
    Path::new(proto_path)
        .file_stem()
        .unwrap()
        .to_str()
        .unwrap()
        .to_string()
}

/// Every message by full name (".yb.master.Foo").
fn messages(files: &[FileDescriptorProto]) -> BTreeMap<String, &DescriptorProto> {
    fn add<'a>(
        out: &mut BTreeMap<String, &'a DescriptorProto>,
        scope: &str,
        msgs: &'a [DescriptorProto],
    ) {
        for m in msgs {
            let full = format!("{scope}.{}", m.name());
            out.insert(full.clone(), m);
            add(out, &full, &m.nested_type);
        }
    }
    let mut out = BTreeMap::new();
    for f in files {
        add(&mut out, &format!(".{}", f.package()), &f.message_type);
    }
    out
}

fn custom_service_name(service: &protobuf::descriptor::ServiceDescriptorProto) -> Option<String> {
    let options = service.options.as_ref()?;
    match options
        .special_fields
        .unknown_fields()
        .get(CUSTOM_SERVICE_NAME_OPTION)?
    {
        UnknownValueRef::LengthDelimited(bytes) => Some(String::from_utf8(bytes.to_vec()).unwrap()),
        _ => None,
    }
}

fn is_root(file: &FileDescriptorProto) -> bool {
    ROOT_DIRS
        .iter()
        .any(|dir| file.name().starts_with(&format!("{dir}/")))
}

fn write_services(files: &[FileDescriptorProto], out: &Path) {
    let msgs = messages(files);
    let mut code = String::from("/// Every RPC service declared in the root `.proto` files.\n");
    code.push_str("pub static SERVICES: &[ServiceDesc] = &[\n");
    for f in files.iter().filter(|f| is_root(f)) {
        for s in &f.service {
            let proto_name = format!("{}.{}", f.package(), s.name());
            let wire_name = custom_service_name(s).unwrap_or_else(|| proto_name.clone());
            writeln!(
                code,
                "    ServiceDesc {{ proto_name: {proto_name:?}, wire_name: {wire_name:?}, methods: &["
            )
            .unwrap();
            for m in &s.method {
                let response = msgs
                    .get(m.output_type())
                    .unwrap_or_else(|| panic!("unknown type {}", m.output_type()));
                let error_field = response
                    .field
                    .iter()
                    .find(|fd| {
                        fd.name() == "error"
                            && fd.type_() == field_descriptor_proto::Type::TYPE_MESSAGE
                    })
                    .map(|fd| (fd.number(), fd.type_name()[1..].to_string()));
                let has_required = response
                    .field
                    .iter()
                    .any(|fd| fd.label() == field_descriptor_proto::Label::LABEL_REQUIRED);
                writeln!(
                    code,
                    "        MethodDesc {{ name: {:?}, request_type: {:?}, response_type: {:?}, \
                     error_field: {:?}, response_has_required_fields: {} }},",
                    m.name(),
                    &m.input_type()[1..],
                    &m.output_type()[1..],
                    error_field,
                    has_required
                )
                .unwrap();
            }
            code.push_str("    ]},\n");
        }
    }
    code.push_str("];\n");
    fs::write(out.join("services.rs"), code).unwrap();
}

fn write_files(files: &[FileDescriptorProto], out: &Path) {
    let mut code = String::from(
        "/// The descriptors of every generated `.proto` file.\n\
         pub fn file_descriptors() -> Vec<protobuf::reflect::FileDescriptor> {\n    vec![\n",
    );
    for f in files {
        writeln!(
            code,
            "        crate::pb::{}::file_descriptor().clone(),",
            module_name(f.name())
        )
        .unwrap();
    }
    code.push_str("    ]\n}\n");
    fs::write(out.join("files.rs"), code).unwrap();
}

fn main() {
    let manifest_dir = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let src_dir = manifest_dir.join("../../..").canonicalize().unwrap();
    let out_dir = PathBuf::from(std::env::var("OUT_DIR").unwrap());
    let proto_dir = out_dir.join("protos");
    let _ = fs::remove_dir_all(&proto_dir);

    let closure = proto_closure(&src_dir);
    let inputs = copy_renamed(&src_dir, &closure, &proto_dir);

    let mut parsed = protobuf_parse::Parser::new()
        .pure()
        .include(&proto_dir)
        .inputs(&inputs)
        .parse_and_typecheck()
        .expect("parsing protos");
    for file in &mut parsed.file_descriptors {
        sort_fields_by_number(&mut file.message_type);
    }

    let pb_dir = out_dir.join("pb");
    let _ = fs::remove_dir_all(&pb_dir);
    fs::create_dir_all(&pb_dir).unwrap();
    protobuf_codegen::gen_and_write::gen_and_write(
        &parsed.file_descriptors,
        &parsed.parser,
        &parsed.relative_paths,
        &pb_dir,
        &protobuf_codegen::Customize::default(),
        &NoCustomization,
    )
    .expect("generating Rust code for protos");

    let files: Vec<FileDescriptorProto> = parsed
        .file_descriptors
        .into_iter()
        .filter(|f| !f.name().starts_with("google/protobuf/"))
        .collect();

    write_services(&files, &out_dir);
    write_files(&files, &out_dir);
    println!("cargo:rerun-if-changed=build.rs");
}
