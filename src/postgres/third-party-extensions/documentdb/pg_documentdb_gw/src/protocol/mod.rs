/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/protocol/mod.rs
 *
 *-------------------------------------------------------------------------
 */

use crate::error::{DocumentDBError, Result};

pub mod header;
pub mod message;
pub mod opcode;
pub mod reader;
pub mod util;

pub const MAX_BSON_OBJECT_SIZE: i32 = 16 * 1024 * 1024;
pub const MAX_MESSAGE_SIZE_BYTES: i32 = 48000000;
pub const LOGICAL_SESSION_TIMEOUT_MINUTES: u8 = 30;

pub const OK_SUCCEEDED: f64 = 1.0;
pub const OK_FAILED: f64 = 0.0;

pub fn extract_database_and_collection_names(path: &str) -> Result<(&str, &str)> {
    let pos = path.find('.').ok_or(DocumentDBError::bad_value(format!(
        "Collection path {path} does not contain a '.'"
    )))?;
    let db = &path[0..pos];
    let coll = &path[pos + 1..];
    Ok((db, coll))
}
