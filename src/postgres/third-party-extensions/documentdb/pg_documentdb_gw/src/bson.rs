/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson.rs
 *
 *-------------------------------------------------------------------------
 */

use std::io::Cursor;

use bson::{spec::ElementType, RawBsonRef, RawDocument};

use crate::{error::DocumentDBError, protocol::util::SyncLittleEndianRead};

/// Read a document's raw BSON bytes from the provided reader.
pub fn read_document_bytes<'a>(
    cursor: &mut Cursor<&'a [u8]>,
) -> Result<(&'a RawDocument, usize), DocumentDBError> {
    let data = &cursor.clone().into_inner()[cursor.position() as usize..];
    let length = cursor.read_i32_sync()?;
    let doc = RawDocument::from_bytes(&data[0..length as usize])?;
    cursor.set_position(cursor.position() + length as u64 - 4);
    Ok((doc, length as usize))
}

pub fn convert_to_f64(bson: RawBsonRef) -> Option<f64> {
    match bson.element_type() {
        ElementType::Double => Some(bson.as_f64().expect("checked")),
        ElementType::Int32 => Some(bson.as_i32().expect("checked") as f64),
        ElementType::Int64 => Some(bson.as_i64().expect("checked") as f64),
        _ => None,
    }
}

pub fn convert_to_bool(bson: RawBsonRef) -> Option<bool> {
    match bson.element_type() {
        ElementType::Boolean => Some(bson.as_bool().expect("checked")),
        ElementType::Double => Some(bson.as_f64().expect("checked") != 0.0),
        ElementType::Int32 => Some(bson.as_i32().expect("checked") != 0),
        ElementType::Int64 => Some(bson.as_i64().expect("checked") != 0),
        _ => None,
    }
}
