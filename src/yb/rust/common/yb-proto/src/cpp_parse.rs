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

//! Where a rust-protobuf parse differs from the C++ runtime's.

use protobuf::descriptor::field_descriptor_proto::Type;
use protobuf::reflect::{
    FieldDescriptor, ReflectValueBox, ReflectValueRef, RuntimeFieldType, RuntimeType, Syntax,
};
use protobuf::{CodedOutputStream, MessageDyn};

fn unknown_enum_value(value: &ReflectValueRef) -> Option<i32> {
    match value {
        ReflectValueRef::Enum(descriptor, number) => descriptor
            .value_by_number(*number)
            .is_none()
            .then_some(*number),
        _ => None,
    }
}

fn is_proto2(m: &dyn MessageDyn) -> bool {
    m.descriptor_dyn().file_descriptor().syntax() == Syntax::Proto2
}

/// Whether [`move_unknown_enum_values`] would change `m`.
fn has_unknown_enum_values(m: &dyn MessageDyn) -> bool {
    let proto2 = is_proto2(m);
    m.descriptor_dyn()
        .fields()
        .any(|f| match f.runtime_field_type() {
            RuntimeFieldType::Singular(RuntimeType::Enum(_)) => {
                proto2
                    && f.get_singular(m)
                        .is_some_and(|v| unknown_enum_value(&v).is_some())
            }
            RuntimeFieldType::Repeated(RuntimeType::Enum(_)) => {
                let values = f.get_repeated(m);
                proto2 && (0..values.len()).any(|i| unknown_enum_value(&values.get(i)).is_some())
            }
            RuntimeFieldType::Singular(RuntimeType::Message(_)) => f
                .get_singular(m)
                .and_then(|v| v.to_message())
                .is_some_and(|nested| has_unknown_enum_values(&*nested)),
            RuntimeFieldType::Repeated(RuntimeType::Message(_)) => {
                let values = f.get_repeated(m);
                (0..values.len()).any(|i| {
                    values
                        .get(i)
                        .to_message()
                        .is_some_and(|nested| has_unknown_enum_values(&*nested))
                })
            }
            RuntimeFieldType::Map(_, RuntimeType::Enum(_)) => {
                proto2
                    && (&f.get_map(m))
                        .into_iter()
                        .any(|(_, v)| unknown_enum_value(&v).is_some())
            }
            RuntimeFieldType::Map(_, RuntimeType::Message(_)) => {
                (&f.get_map(m)).into_iter().any(|(_, v)| {
                    v.to_message()
                        .is_some_and(|nested| has_unknown_enum_values(&*nested))
                })
            }
            _ => false,
        })
}

/// The wire type of a map field's key, from the map entry message nested in `m`'s type.
fn map_key_type(m: &dyn MessageDyn, field: &FieldDescriptor) -> Option<Type> {
    let entry_name = field.proto().type_name().rsplit('.').next()?;
    m.descriptor_dyn()
        .proto()
        .nested_type
        .iter()
        .find(|nested| nested.name() == entry_name)
        .and_then(|entry| entry.field.iter().find(|f| f.number() == 1))
        .map(|key| key.type_())
}

/// One map entry as C++ keeps it in the unknown fields: the key (field 1), then the value
/// (field 2).
fn encode_map_entry(key_type: Type, key: &ReflectValueRef, value: i32) -> Option<Vec<u8>> {
    let mut bytes = vec![];
    let mut os = CodedOutputStream::vec(&mut bytes);
    let written = match (key_type, key) {
        (Type::TYPE_STRING, ReflectValueRef::String(v)) => os.write_string(1, v),
        (Type::TYPE_BYTES, ReflectValueRef::Bytes(v)) => os.write_bytes(1, v),
        (Type::TYPE_INT32, ReflectValueRef::I32(v)) => os.write_int32(1, *v),
        (Type::TYPE_SINT32, ReflectValueRef::I32(v)) => os.write_sint32(1, *v),
        (Type::TYPE_SFIXED32, ReflectValueRef::I32(v)) => os.write_sfixed32(1, *v),
        (Type::TYPE_INT64, ReflectValueRef::I64(v)) => os.write_int64(1, *v),
        (Type::TYPE_SINT64, ReflectValueRef::I64(v)) => os.write_sint64(1, *v),
        (Type::TYPE_SFIXED64, ReflectValueRef::I64(v)) => os.write_sfixed64(1, *v),
        (Type::TYPE_UINT32, ReflectValueRef::U32(v)) => os.write_uint32(1, *v),
        (Type::TYPE_FIXED32, ReflectValueRef::U32(v)) => os.write_fixed32(1, *v),
        (Type::TYPE_UINT64, ReflectValueRef::U64(v)) => os.write_uint64(1, *v),
        (Type::TYPE_FIXED64, ReflectValueRef::U64(v)) => os.write_fixed64(1, *v),
        (Type::TYPE_BOOL, ReflectValueRef::Bool(v)) => os.write_bool(1, *v),
        _ => return None,
    };
    written.ok()?;
    os.write_enum(2, value).ok()?;
    os.flush().ok()?;
    drop(os);
    Some(bytes)
}

fn add_unknown_varint(m: &mut dyn MessageDyn, field: &FieldDescriptor, value: i32) {
    // Negative enum values are sign-extended on the wire, as int32 fields are.
    m.mut_unknown_fields_dyn()
        .add_varint(field.number() as u32, value as i64 as u64);
}

/// Moves values of proto2 enum fields that the enum does not define into the unknown fields, at
/// any depth, as the C++ runtime does while parsing. rust-protobuf keeps such a value in the field,
/// so without this a field the C++ master sees as unset would be set, and the message would
/// serialize the value in field order rather than with the unknown fields.
///
/// In a map with enum values, an entry whose value is unknown moves whole, as C++ keeps it: a
/// length-delimited unknown field holding the entry's key and value.
///
/// A difference that remains: the values land after unknown fields already in the message, where
/// C++ interleaves them in wire order.
pub fn move_unknown_enum_values(m: &mut dyn MessageDyn) {
    if !has_unknown_enum_values(m) {
        return;
    }
    let proto2 = is_proto2(m);
    for field in m.descriptor_dyn().fields() {
        match field.runtime_field_type() {
            RuntimeFieldType::Singular(RuntimeType::Enum(_)) if proto2 => {
                let unknown = field.get_singular(m).and_then(|v| unknown_enum_value(&v));
                if let Some(value) = unknown {
                    field.clear_field(m);
                    add_unknown_varint(m, &field, value);
                }
            }
            RuntimeFieldType::Repeated(RuntimeType::Enum(descriptor)) if proto2 => {
                let values = field.get_repeated(m);
                let numbers: Vec<i32> = (0..values.len())
                    .filter_map(|i| match values.get(i) {
                        ReflectValueRef::Enum(_, number) => Some(number),
                        _ => None,
                    })
                    .collect();
                if numbers
                    .iter()
                    .all(|n| descriptor.value_by_number(*n).is_some())
                {
                    continue;
                }
                let mut repeated = field.mut_repeated(m);
                repeated.clear();
                let mut unknown = vec![];
                for number in numbers {
                    if descriptor.value_by_number(number).is_some() {
                        repeated.push(ReflectValueBox::Enum(descriptor.clone(), number));
                    } else {
                        unknown.push(number);
                    }
                }
                for number in unknown {
                    add_unknown_varint(m, &field, number);
                }
            }
            RuntimeFieldType::Singular(RuntimeType::Message(_)) => {
                if field.has_field(m) {
                    move_unknown_enum_values(field.mut_message(m));
                }
            }
            RuntimeFieldType::Repeated(RuntimeType::Message(_)) => {
                let values = field.get_repeated(m);
                let mut messages: Vec<Box<dyn MessageDyn>> = (0..values.len())
                    .filter_map(|i| values.get(i).to_message().map(|v| v.clone_box()))
                    .collect();
                if !messages.iter().any(|v| has_unknown_enum_values(&**v)) {
                    continue;
                }
                for message in &mut messages {
                    move_unknown_enum_values(&mut **message);
                }
                let mut repeated = field.mut_repeated(m);
                repeated.clear();
                for message in messages {
                    repeated.push(ReflectValueBox::Message(message));
                }
            }
            RuntimeFieldType::Map(_, RuntimeType::Enum(descriptor)) if proto2 => {
                let Some(key_type) = map_key_type(m, &field) else {
                    continue;
                };
                let entries: Vec<(ReflectValueBox, i32)> = (&field.get_map(m))
                    .into_iter()
                    .filter_map(|(k, v)| match v {
                        ReflectValueRef::Enum(_, number) => Some((k.to_box(), number)),
                        _ => None,
                    })
                    .collect();
                if entries
                    .iter()
                    .all(|(_, n)| descriptor.value_by_number(*n).is_some())
                {
                    continue;
                }
                let mut unknown = vec![];
                let mut map = field.mut_map(m);
                map.clear();
                for (key, number) in entries {
                    if descriptor.value_by_number(number).is_some() {
                        map.insert(key, ReflectValueBox::Enum(descriptor.clone(), number));
                    } else {
                        unknown.push((key, number));
                    }
                }
                for (key, number) in unknown {
                    if let Some(entry) = encode_map_entry(key_type, &key.as_value_ref(), number) {
                        m.mut_unknown_fields_dyn()
                            .add_length_delimited(field.number() as u32, entry);
                    }
                }
            }
            RuntimeFieldType::Map(_, RuntimeType::Message(_)) => {
                let mut entries: Vec<(ReflectValueBox, Box<dyn MessageDyn>)> = (&field.get_map(m))
                    .into_iter()
                    .filter_map(|(k, v)| v.to_message().map(|v| (k.to_box(), v.clone_box())))
                    .collect();
                if !entries.iter().any(|(_, v)| has_unknown_enum_values(&**v)) {
                    continue;
                }
                for (_, message) in &mut entries {
                    move_unknown_enum_values(&mut **message);
                }
                let mut map = field.mut_map(m);
                map.clear();
                for (key, message) in entries {
                    map.insert(key, ReflectValueBox::Message(message));
                }
            }
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use protobuf::{Message, UnknownValueRef};

    use super::*;
    use crate::pb::catalog_entity_info::{
        backfill_job_pb, sys_tablets_entry_pb, BackfillJobPB, SysTabletsEntryPB,
        SysXClusterConfigEntryPB,
    };

    #[test]
    fn unknown_enum_value_moves_to_unknown_fields() {
        let mut tablet = SysTabletsEntryPB::new();
        tablet.set_table_id(b"t".to_vec());
        tablet.set_state(sys_tablets_entry_pb::State::RUNNING);
        let mut bytes = tablet.write_to_bytes().unwrap();
        // Field 4 (state), value 77: a state this build does not know.
        let state_tag = (4 << 3) as u8;
        let at = bytes.iter().position(|b| *b == state_tag).unwrap();
        bytes[at + 1] = 77;

        let mut parsed = SysTabletsEntryPB::parse_from_bytes(&bytes).unwrap();
        assert!(parsed.state.is_some());
        move_unknown_enum_values(&mut parsed);
        assert!(parsed.state.is_none());
        let unknown: Vec<_> = parsed.special_fields.unknown_fields().iter().collect();
        assert_eq!(unknown, vec![(4, UnknownValueRef::Varint(77))]);
        // Serialized as C++ does: the value after the known fields.
        let out = parsed.write_to_bytes().unwrap();
        assert_eq!(&out[out.len() - 2..], &[state_tag, 77]);
    }

    #[test]
    fn a_map_entry_with_an_unknown_enum_value_moves_whole() {
        // BackfillJobPB.backfill_state (field 2) is map<string, State>: entry "a" -> SUCCESS (2),
        // entry "b" -> 77, which this build does not define.
        let entry = |key: u8, value: u8| vec![0x0a, 1, key, 0x10, value];
        let mut bytes = vec![];
        for e in [entry(b'a', 2), entry(b'b', 77)] {
            bytes.extend([0x12, e.len() as u8]);
            bytes.extend(e);
        }

        let mut parsed = BackfillJobPB::parse_from_bytes(&bytes).unwrap();
        assert_eq!(parsed.backfill_state.len(), 2);
        move_unknown_enum_values(&mut parsed);
        assert_eq!(parsed.backfill_state.len(), 1);
        assert_eq!(
            parsed.backfill_state["a"].enum_value(),
            Ok(backfill_job_pb::State::SUCCESS)
        );
        let unknown: Vec<_> = parsed.special_fields.unknown_fields().iter().collect();
        assert_eq!(
            unknown,
            vec![(2, UnknownValueRef::LengthDelimited(&entry(b'b', 77)))]
        );
    }

    #[test]
    fn unknown_enum_values_in_map_message_values_move() {
        // SysXClusterConfigEntryPB.xcluster_info_per_namespace (field 3) maps a namespace id to
        // an XClusterNamespaceInfoPB, whose role (field 1) is set to 77, a value this build does
        // not define.
        let entry = [0x0a, 1, b'n', 0x12, 2, 0x08, 77];
        let mut bytes = vec![0x1a, entry.len() as u8];
        bytes.extend(entry);

        let mut parsed = SysXClusterConfigEntryPB::parse_from_bytes(&bytes).unwrap();
        assert!(parsed.xcluster_info_per_namespace["n"].role.is_some());
        move_unknown_enum_values(&mut parsed);
        let info = &parsed.xcluster_info_per_namespace["n"];
        assert!(info.role.is_none());
        let unknown: Vec<_> = info.special_fields.unknown_fields().iter().collect();
        assert_eq!(unknown, vec![(1, UnknownValueRef::Varint(77))]);
    }
}
