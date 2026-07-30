/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/responses/writer.rs
 *
 *-------------------------------------------------------------------------
 */

use crate::{
    context::ConnectionContext,
    error::{DocumentDBError, Result},
    protocol::{header::Header, opcode::OpCode},
    CommandError, GwStream, Response,
};
use bson::RawDocument;
use tokio::io::AsyncWriteExt;

/// Write a server response to the client stream
pub async fn write(header: &Header, response: &Response, stream: &mut GwStream) -> Result<()> {
    write_and_flush(header, response.as_raw_document()?, stream).await
}

/// Write a raw BSON object to the client stream
pub async fn write_and_flush(
    header: &Header,
    response: &RawDocument,
    stream: &mut GwStream,
) -> Result<()> {
    // The format of the response will depend on the OP which the client sent
    match header.op_code {
        OpCode::Command => unimplemented!(),

        // Messages are always responded to with messages
        OpCode::Msg => write_message(header, response, stream).await,

        // Query is responded to with Reply
        OpCode::Query => {
            // Write the header
            let header = Header {
                // Total size of the response is the bytes + standard header + reply header
                length: (response.as_bytes().len() + Header::LENGTH + 20) as i32,
                request_id: header.request_id,
                response_to: header.request_id,
                op_code: OpCode::Reply,
            };
            header.write_to(stream).await?;

            stream.write_i32_le(0).await?; // Response flags
            stream.write_i64_le(0).await?; // Cursor Id
            stream.write_i32_le(0).await?; // startingFrom
            stream.write_i32_le(1).await?; // numberReturned

            stream.write_all(response.as_bytes()).await?;
            Ok(())
        }

        // Insert has no response
        OpCode::Insert => Ok(()),
        _ => Err(DocumentDBError::internal_error(format!(
            "Unexpected response opcode: {:?}",
            header.op_code
        ))),
    }?;

    stream.flush().await?;

    Ok(())
}

/// Serializes the Message to bytes and writes them to `writer`.
pub async fn write_message(
    header: &Header,
    response: &RawDocument,
    writer: &mut GwStream,
) -> Result<()> {
    let total_length = Header::LENGTH
        + std::mem::size_of::<u32>()
        + std::mem::size_of::<u8>()
        + response.as_bytes().len(); // Message payload + status code

    let header = Header {
        length: total_length as i32,
        request_id: header.request_id,
        response_to: header.request_id,
        op_code: OpCode::Msg,
    };
    header.write_to(writer).await?;

    // Write Flags
    writer.write_u32_le(0).await?;

    // Write payload type + section
    writer.write_u8(0).await?;

    writer.write_all(response.as_bytes()).await?;

    Ok(())
}

pub async fn write_error_without_header(
    connection_context: &ConnectionContext,
    err: DocumentDBError,
    stream: &mut GwStream,
    activity_id: &str,
) -> Result<()> {
    let response = CommandError::from_error(connection_context, &err, activity_id)
        .await
        .to_raw_document_buf();

    let header = Header {
        length: (response.as_bytes().len() + 1) as i32,
        request_id: 0,
        response_to: 0,
        op_code: OpCode::Msg,
    };

    write_and_flush(&header, &response, stream).await?;

    Ok(())
}
