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

use protobuf::reflect::{
    FieldDescriptor, ReflectValueBox, ReflectValueRef, RuntimeFieldType, RuntimeType, Syntax,
};
use protobuf::MessageDyn;

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
            _ => false,
        })
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
/// Differences that remain: the values land after unknown fields already in the message, where
/// C++ interleaves them in wire order, and unknown enum values in map entries stay in place.
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
            _ => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use protobuf::{Message, UnknownValueRef};

    use super::*;
    use crate::pb::catalog_entity_info::{sys_tablets_entry_pb, SysTabletsEntryPB};

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
}
