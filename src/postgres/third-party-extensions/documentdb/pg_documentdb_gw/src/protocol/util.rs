/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/protocol/util.rs
 *
 *-------------------------------------------------------------------------
 */

use std::io::Read;

use crate::error::DocumentDBError;

pub trait SyncLittleEndianRead: Read {
    /// Read an `i32` in little-endian order.
    fn read_i32_sync(&mut self) -> Result<i32, DocumentDBError> {
        let mut buf: [u8; 4] = [0; 4];
        self.read_exact(&mut buf)?;
        Ok(i32::from_le_bytes(buf))
    }

    /// Read a `u32` in little-endian order.
    fn read_u32_sync(&mut self) -> Result<u32, DocumentDBError> {
        let mut buf: [u8; 4] = [0; 4];
        self.read_exact(&mut buf)?;
        Ok(u32::from_le_bytes(buf))
    }

    fn read_u8_sync(&mut self) -> Result<u8, DocumentDBError> {
        let mut buf: [u8; 1] = [0; 1];
        self.read_exact(&mut buf)?;
        Ok(buf[0])
    }
}

impl<R: Read> SyncLittleEndianRead for R {}
