/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/protocol/message.rs
 *
 *-------------------------------------------------------------------------
 */

use std::io::Cursor;

use ::bson::RawDocument;
use bitflags::bitflags;

use crate::{bson, error::DocumentDBError};

use super::{reader::str_from_u8_nul_utf8, util::SyncLittleEndianRead};

#[derive(Debug)]
pub struct Message<'a> {
    pub(crate) _response_to: i32,
    pub(crate) _flags: MessageFlags,
    pub(crate) sections: Vec<MessageSection<'a>>,
    pub(crate) _checksum: Option<u32>,
    pub(crate) _request_id: Option<i32>,
}

const _DEFAULT_MAX_MESSAGE_SIZE_BYTES: i32 = 48 * 1024 * 1024;

impl Message<'_> {
    pub fn read_from_op_msg<'a>(
        mut reader: Cursor<&'a [u8]>,
        response_to: i32,
    ) -> Result<Message<'a>, DocumentDBError> {
        let mut length_remaining = reader.get_ref().len();
        let flags = MessageFlags::from_bits_truncate(reader.read_u32_sync()?);
        length_remaining -= std::mem::size_of::<u32>();

        let mut sections = Vec::new();
        let mut bytes_read = 0;

        while length_remaining - bytes_read > 4 {
            let section = MessageSection::read(&mut reader)?;
            bytes_read += section.length() + 1; // 1 for the payload type
            sections.push(section);
        }

        length_remaining -= bytes_read;

        let mut checksum = None;

        if length_remaining == 4 && flags.contains(MessageFlags::CHECKSUM_PRESENT) {
            checksum = Some(reader.read_u32_sync()?);
        } else if length_remaining != 0 {
            return Err(DocumentDBError::bad_value(format!(
                "Malformed message. Expecting {length_remaining} more bytes of data"
            )));
        }

        // Some drivers don't put the command document first.
        sections.sort_by_key(|a| a.payload_type());

        Ok(Message {
            _response_to: response_to,
            _flags: flags,
            sections,
            _checksum: checksum,
            _request_id: None,
        })
    }
}

/// Represents a section as defined by the OP_MSG definition in the driver.
#[derive(Debug)]
pub(crate) enum MessageSection<'a> {
    Document(&'a RawDocument),
    Sequence {
        size: i32,
        _identifier: &'a str,
        documents: &'a [u8],
    },
}

impl MessageSection<'_> {
    fn length(&self) -> usize {
        match self {
            Self::Document(r) => r.as_bytes().len(),
            Self::Sequence { size, .. } => *size as usize,
        }
    }

    /// Reads bytes from `reader` and deserializes them into a MessageSection.
    fn read<'b>(reader: &mut Cursor<&'b [u8]>) -> Result<MessageSection<'b>, DocumentDBError> {
        let payload_type = reader.read_u8_sync()?;

        if payload_type == 0 {
            let (doc, _) = bson::read_document_bytes(reader)?;

            return Ok(MessageSection::Document(doc));
        }

        let size = reader.read_i32_sync()? as usize;
        let end = reader.position() + (size - 4) as u64;

        let (identifier, id_size) =
            str_from_u8_nul_utf8(&reader.get_ref()[reader.position() as usize..])?;
        reader.set_position(reader.position() + id_size as u64 + 1);

        let pos = reader.position() as usize;
        let documents = &reader.get_ref()[pos..end as usize];
        reader.set_position(end);
        Ok(MessageSection::Sequence {
            size: size as i32,
            _identifier: identifier,
            documents,
        })
    }

    fn payload_type(&self) -> i32 {
        match self {
            MessageSection::Document(_) => 0,
            MessageSection::Sequence {
                size: _,
                _identifier: _,
                documents: _,
            } => 1,
        }
    }
}

bitflags! {
    /// Represents the bitwise flags for an OP_MSG as defined in the c driver.
    pub(crate) struct MessageFlags: u32 {
        const NONE             = 0b_0000_0000_0000_0000_0000_0000_0000_0000;
        const CHECKSUM_PRESENT = 0b_0000_0000_0000_0000_0000_0000_0000_0001;
        const MORE_TO_COME     = 0b_0000_0000_0000_0000_0000_0000_0000_0010;
        const EXHAUST_ALLOWED  = 0b_0000_0000_0000_0001_0000_0000_0000_0000;
    }
}
