# Protobuf in Rust

`common/yb-proto` generates Rust messages at build time from the `.proto` files under `src/yb`,
never from copies. It covers the files in `ROOT_DIRS` (in `build.rs`; today `yb/master`) and
everything they import. A component adds its directory when it moves to Rust.

## Requirements

Persisted messages carry fields that older or newer releases may not know, and during an upgrade
or rollback an older server can rewrite them. The C++ runtime keeps unknown fields and writes them
back. The Rust side must too, and it must also keep proto2 presence and defaults, per-file proto3
packing, and `required` checks on parse.

The test for all of that is byte equality with the C++ runtime: for a message the C++ side wrote,
Rust's parse-and-serialize must produce exactly what C++'s parse-and-serialize produces. The input
bytes are not the reference, because C++ itself moves unknown fields to the end.

## Crate choice: rust-protobuf 3

- **prost** drops unknown fields. Ruled out.
- **Google's Rust protobuf (`protobuf` 4.x)**: its C++ kernel needs a C++ runtime of v30 or newer,
  and thirdparty ships 3.21.12. Its upb kernel needs a newer `protoc` at build time than thirdparty
  ships, which would be a new thirdparty tool. It also has no Rust reflection or text-format
  parser.
- **rust-protobuf 3 (`protobuf` 3.7)** generates code in pure Rust, without `protoc`. It keeps
  unknown fields and has proto2 presence, defaults and reflection. Chosen.

Text format, which `DumpSysCatalogEntries` and `WriteSysCatalogEntry` use, comes from the C++
runtime in the host process, not from this crate.

## What the build script changes

- **Field order.** rust-protobuf serializes fields in declaration order; C++ serializes them in
  field-number order. Many YugabyteDB messages declare a higher-numbered field before a lower one,
  so `build.rs` sorts each message's fields by number before generating code. Without that, 870 of
  896 rows of a real sys catalog re-serialized with different bytes.
- **File names.** rust-protobuf names a module after the file's basename, so
  `yb/consensus/metadata.proto` and `yb/tablet/metadata.proto` are compiled as
  `consensus_metadata` and `tablet_metadata`. Packages and the wire format do not change.

Map fields have no defined wire order in either runtime; checks that compare bytes fall back to
comparing content for messages with maps.

## Service tables

`yb_proto::SERVICES` lists every RPC service in the root files with its wire name
(`custom_service_name` when set), methods, and each response's `error` field. A server uses it to
register services and to answer methods it does not implement with an error, without hand-written
per-method code. A response with `required` fields of its own cannot be answered with only `error`
set; `MethodDesc::response_has_required_fields` marks those.
