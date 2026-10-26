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

//! Master sys-catalog entities.
//!
//! Layer 1 holds only rows by `SysRowEntryType`, mirroring `CATALOG_ENTITY_TYPE_MAP` in
//! src/yb/master/catalog_entity_types.h: parsing them and serializing them again exactly as the C++
//! master does.

use std::fmt;

use protobuf::reflect::MessageDescriptor;
use yb_proto::pb::master_types::SysRowEntryType;

/// Why a sys-catalog row could not be parsed.
#[derive(Debug)]
pub enum EntityError {
    /// The entry type has no message type (`UNKNOWN`, or a value newer than this build).
    UnknownType(i32),
    /// The bytes are not a valid message of the expected type.
    Parse(protobuf::Error),
}

impl fmt::Display for EntityError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            EntityError::UnknownType(t) => write!(f, "unsupported catalog entry type {t}"),
            EntityError::Parse(e) => write!(f, "parse error: {e}"),
        }
    }
}

impl std::error::Error for EntityError {}

impl From<protobuf::Error> for EntityError {
    fn from(e: protobuf::Error) -> Self {
        EntityError::Parse(e)
    }
}

include!(concat!(env!("OUT_DIR"), "/catalog_entity_types.rs"));

/// The message type of rows of the given type.
pub fn entity_descriptor(entry_type: SysRowEntryType) -> Result<MessageDescriptor, EntityError> {
    let name = CATALOG_ENTITY_TYPES
        .iter()
        .find(|(t, _)| *t == entry_type)
        .map(|(_, name)| *name)
        .ok_or(EntityError::UnknownType(entry_type as i32))?;
    Ok(yb_proto::message_descriptor(name)
        .unwrap_or_else(|| panic!("{name} is in catalog_entity_types.h but was not generated")))
}

/// Parses a sys-catalog row of the given type. Like the C++ master's parse, it fails when a
/// required field is missing.
pub fn parse_entity(
    entry_type: SysRowEntryType,
    data: &[u8],
) -> Result<Box<dyn protobuf::MessageDyn>, EntityError> {
    Ok(entity_descriptor(entry_type)?.parse_from_bytes(data)?)
}

/// Parses a row and serializes it again, keeping unknown fields. For a row the C++ master wrote,
/// the result must equal what the C++ runtime produces for the same parse and serialize; the
/// conformance tests check exactly that.
pub fn reserialize_entity(
    entry_type: SysRowEntryType,
    data: &[u8],
) -> Result<Vec<u8>, EntityError> {
    Ok(parse_entity(entry_type, data)?.write_to_bytes_dyn()?)
}

#[cfg(test)]
mod tests {
    use protobuf::{Enum, Message, UnknownValueRef};

    use super::*;
    use yb_proto::pb::catalog_entity_info::{sys_tablets_entry_pb, SysTabletsEntryPB};

    fn assert_fields_sorted(message: &protobuf::reflect::MessageDescriptor) {
        let numbers: Vec<i32> = message.fields().map(|f| f.number()).collect();
        let mut sorted = numbers.clone();
        sorted.sort_unstable();
        assert_eq!(numbers, sorted, "{}", message.full_name());
        for nested in message.nested_messages() {
            assert_fields_sorted(&nested);
        }
    }

    #[test]
    fn fields_are_generated_in_number_order() {
        // Field order in generated code is serialization order; it must be the C++ runtime's.
        for file in [
            yb_proto::pb::catalog_entity_info::file_descriptor(),
            yb_proto::pb::master_types::file_descriptor(),
            yb_proto::pb::common::file_descriptor(),
        ] {
            for message in file.messages() {
                assert_fields_sorted(&message);
            }
        }
    }

    #[test]
    fn every_entry_type_is_mapped() {
        let mapped: Vec<_> = CATALOG_ENTITY_TYPES.iter().map(|(t, _)| *t).collect();
        for t in SysRowEntryType::VALUES {
            if *t != SysRowEntryType::UNKNOWN {
                assert!(mapped.contains(t), "{t:?} missing");
            }
        }
        assert!(matches!(
            parse_entity(SysRowEntryType::UNKNOWN, &[]),
            Err(EntityError::UnknownType(0))
        ));
    }

    #[test]
    fn proto2_default_is_not_presence() {
        // SysTabletsEntryPB.state defaults to UNKNOWN (999), and loaders treat "unset" differently
        // from PREPARING (wire value 0).
        let unset = SysTabletsEntryPB::new();
        assert!(unset.state.is_none());
        assert_eq!(unset.state(), sys_tablets_entry_pb::State::UNKNOWN);
        let mut preparing = SysTabletsEntryPB::new();
        preparing.set_table_id(b"t".to_vec());
        preparing.set_state(sys_tablets_entry_pb::State::PREPARING);
        let bytes = preparing.write_to_bytes().unwrap();
        let parsed = SysTabletsEntryPB::parse_from_bytes(&bytes).unwrap();
        assert_eq!(
            parsed.state,
            Some(sys_tablets_entry_pb::State::PREPARING.into())
        );
    }

    /// Appends one unknown field of each wire type, with numbers no release uses.
    fn with_unknown_fields(mut bytes: Vec<u8>) -> Vec<u8> {
        let mut extra = protobuf::UnknownFields::new();
        extra.add_varint(9001, 150);
        extra.add_fixed64(9002, 0x0102_0304_0506_0708);
        extra.add_length_delimited(9003, b"newer-release".to_vec());
        extra.add_fixed32(9004, 7);
        let mut tail = Vec::new();
        {
            let mut os = protobuf::CodedOutputStream::vec(&mut tail);
            extra.write_to_bytes_dyn_helper(&mut os);
        }
        bytes.extend(tail);
        bytes
    }

    trait WriteUnknown {
        fn write_to_bytes_dyn_helper(&self, os: &mut protobuf::CodedOutputStream<'_>);
    }

    impl WriteUnknown for protobuf::UnknownFields {
        fn write_to_bytes_dyn_helper(&self, os: &mut protobuf::CodedOutputStream<'_>) {
            os.write_unknown_fields(self).unwrap();
            os.flush().unwrap();
        }
    }

    #[test]
    fn unknown_fields_survive_reserialization() {
        let mut tablet = SysTabletsEntryPB::new();
        tablet.set_table_id(b"0000000000004000800000000000400a".to_vec());
        tablet.set_state(sys_tablets_entry_pb::State::RUNNING);
        let bytes = with_unknown_fields(tablet.write_to_bytes().unwrap());

        let out = reserialize_entity(SysRowEntryType::TABLET, &bytes).unwrap();
        // Known fields first in field order, then unknown fields in the order read: the same
        // layout the C++ runtime produces.
        assert_eq!(out, bytes);
        let parsed = SysTabletsEntryPB::parse_from_bytes(&out).unwrap();
        let unknown = parsed.special_fields.unknown_fields();
        assert_eq!(unknown.get(9001), Some(UnknownValueRef::Varint(150)));
        assert_eq!(
            unknown.get(9003),
            Some(UnknownValueRef::LengthDelimited(b"newer-release"))
        );
        assert_eq!(parsed.state(), sys_tablets_entry_pb::State::RUNNING);
    }

    #[test]
    fn missing_required_field_is_rejected_like_cpp() {
        // pb_util::ParseFromArray uses ParseFromCodedStream, which fails on a missing required
        // field (SysTabletsEntryPB.table_id), so the Rust side must not accept such a row either.
        assert!(matches!(
            parse_entity(SysRowEntryType::TABLET, &[]),
            Err(EntityError::Parse(_))
        ));
    }
}
