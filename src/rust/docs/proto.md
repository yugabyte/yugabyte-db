# Protobuf in Rust

`common/yb-proto` generates Rust messages at build time from the `.proto` files under `src/yb`,
never from copies. It covers the files in `ROOT_DIRS` and `ROOT_FILES` (in `build.rs`; today `yb/master`, plus the
tserver admin and consensus services the tserver simulator serves) and everything they import. A component adds its directory when it moves to Rust.

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
- **Oneofs.** rust-protobuf writes every oneof after all other fields. A oneof with a member
  numbered below some other field (`AppStatusPB.error_codes`, field 4, before fields 6, 7 and 9)
  is generated as plain optional fields, which serialize in number order. The wire format is the
  same; only a message that sets two members of such a oneof parses differently (C++ keeps the
  last, Rust both).
- **File names.** rust-protobuf names a module after the file's basename, so
  `yb/consensus/metadata.proto` and `yb/tablet/metadata.proto` are compiled as
  `consensus_metadata` and `tablet_metadata`. Packages and the wire format do not change.

## Parsing like C++

C++ moves a proto2 enum value its build does not know into the unknown fields, leaving the field
unset; rust-protobuf keeps it in the field. `yb_proto::move_unknown_enum_values` applies the C++
behavior after a parse, and `master-catalog-entity` calls it for every sys-catalog row, so a row
from a newer release re-serializes as C++ would write it.

Map fields have no defined wire order in either runtime; checks that compare bytes fall back to
comparing content for messages with maps.

## Service tables

`yb_proto::SERVICES` lists every RPC service in the root files with its wire name
(`custom_service_name` when set), methods, and each response's `error` field. A server uses it to
register services and to answer methods it does not implement with an error, without hand-written
per-method code. A response with `required` fields of its own cannot be answered with only `error`
set; `MethodDesc::response_has_required_fields` marks those.
