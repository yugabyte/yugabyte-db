/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/protocol/header.rs
 *
 *-------------------------------------------------------------------------
 */

use tokio::io::{AsyncReadExt, AsyncWriteExt};

use crate::{error::Result, protocol::opcode::OpCode, GwStream};

/// Represents the message header (first 16 bytes of wire protocol message).
///
/// The header contains metadata about the message including its length, request/response IDs,
/// and the operation code that determines how to interpret the message body.
#[derive(Debug)]
pub struct Header {
    pub length: i32,
    pub request_id: i32,
    pub response_to: i32,
    pub op_code: OpCode,
}

impl Header {
    /// Size of the header in bytes (always 16 bytes)
    pub const LENGTH: usize = 4 * std::mem::size_of::<i32>();

    /// Writes the header to the provided stream in wire format.
    ///
    /// The header is written in little-endian byte order as required by the wire protocol.
    ///
    /// # Arguments
    /// * `stream` - The stream to write to
    ///
    /// # Errors
    /// Returns an error if writing to the stream fails.
    pub async fn write_to(&self, stream: &mut GwStream) -> Result<()> {
        stream.write_all(&self.length.to_le_bytes()).await?;
        stream.write_all(&self.request_id.to_le_bytes()).await?;
        stream.write_all(&self.response_to.to_le_bytes()).await?;
        stream
            .write_all(&(self.op_code as i32).to_le_bytes())
            .await?;

        Ok(())
    }

    /// Reads a header from the provided stream.
    ///
    /// Reads exactly 16 bytes from the stream and parses them as wire protocol header
    /// in little-endian byte order.
    ///
    /// # Arguments
    /// * `reader` - The stream to read from
    ///
    /// # Returns
    /// The parsed header
    ///
    /// # Errors
    /// Returns an error if:
    /// - Reading from the stream fails
    /// - The stream contains insufficient data
    pub async fn read_from(reader: &mut GwStream) -> Result<Self> {
        let length = reader.read_i32_le().await?;
        let request_id = reader.read_i32_le().await?;
        let response_to = reader.read_i32_le().await?;
        let op_code = OpCode::from_value(reader.read_i32_le().await?);

        Ok(Self {
            length,
            request_id,
            response_to,
            op_code,
        })
    }
}
