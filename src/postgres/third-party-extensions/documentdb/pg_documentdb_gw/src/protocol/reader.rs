/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/protocol/reader.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{
    io::{Cursor, ErrorKind},
    str::FromStr,
};

use bson::{rawdoc, RawArrayBuf, RawDocument, RawDocumentBuf};
use tokio::io::AsyncReadExt;

use crate::{
    error::{DocumentDBError, Result},
    protocol::{extract_database_and_collection_names, opcode::OpCode},
    requests::{Request, RequestMessage, RequestType},
    GwStream,
};

use super::{
    header::Header,
    message::{self, Message, MessageSection},
};

/// Read a standard message header from the client stream
pub async fn read_header(stream: &mut GwStream) -> Result<Option<Header>> {
    match Header::read_from(stream).await {
        Ok(header) => Ok(Some(header)),
        Err(DocumentDBError::IoError(e, b)) => {
            if e.kind() == ErrorKind::UnexpectedEof
                || e.kind() == ErrorKind::BrokenPipe
                || e.kind() == ErrorKind::ConnectionReset
            {
                Ok(None)
            } else {
                Err(DocumentDBError::IoError(e, b))
            }
        }
        Err(e) => Err(e),
    }
}

/// Given an already read header, read the remaining message bytes into a RequestMessage
pub async fn read_request(header: &Header, stream: &mut GwStream) -> Result<RequestMessage> {
    let message_size = usize::try_from(header.length).map_err(|_| {
        DocumentDBError::bad_value("Message length could not be converted to a usize".to_string())
    })?;

    // 16 bytes of the message were already used by the headers
    let mut message: Vec<u8> = vec![0; message_size - Header::LENGTH];

    stream.read_exact(&mut message).await?;

    Ok(RequestMessage {
        request: message,
        op_code: header.op_code,
        request_id: header.request_id,
        response_to: header.response_to,
    })
}

/// Parse a request message into a typed Request
pub async fn parse_request<'a>(
    message: &'a RequestMessage,
    requires_response: &mut bool,
) -> Result<Request<'a>> {
    // Parse the specific message based on OpCode
    let request = match message.op_code {
        OpCode::Query => parse_query(&message.request).await?,
        OpCode::Msg => parse_msg(message, requires_response).await?,
        OpCode::Insert => parse_insert(message).await?,
        _ => Err(DocumentDBError::internal_error(format!(
            "Unimplemented: {:?}",
            message.op_code
        )))?,
    };
    Ok(request)
}

/// Parse a OP_QUERY message
async fn parse_query<'a>(message: &'a [u8]) -> Result<Request<'a>> {
    let mut reader = Cursor::new(message);

    let _flags = reader.read_u32_le().await?;

    // Parse the collection and skip the position to the end of it
    let (collection_path, endpos) = str_from_u8_nul_utf8(&reader.get_ref()[4..])?;

    reader.set_position(u64::try_from(endpos + 5).map_err(|_| {
        DocumentDBError::internal_error("Collection length failed to convert to a u64.".to_string())
    })?);

    let _number_to_skip = reader.read_u32_le().await?;
    let _number_to_return = reader.read_u32_le().await?;

    // Parse the query into a byte array
    let query_size = usize::try_from(reader.read_u32_le().await?).map_err(|_| {
        DocumentDBError::bad_value("Message length could not be converted to a usize".to_string())
    })?;
    let pos = usize::try_from(reader.position()).map_err(|_| {
        DocumentDBError::bad_value("Reader position could not be converted to a usize".to_string())
    })? - 4;
    let query_slice = &reader.get_ref()[pos..pos + query_size];

    // Treat the bytes as a raw bson reference
    let query = RawDocument::from_bytes(query_slice)?;
    let (_db, collection_name) = extract_database_and_collection_names(collection_path)?;

    // OP_QUERY is only supported for commands currently
    if collection_name == "$cmd" {
        return parse_cmd(query, None).await;
    }

    Err(DocumentDBError::internal_error(
        "Unable to parse OpQuery request".to_string(),
    ))
}

/// Read from a byte array until a nul terminator, parse using utf-8
pub fn str_from_u8_nul_utf8(utf8_src: &[u8]) -> Result<(&str, usize)> {
    let nul_range_end =
        utf8_src
            .iter()
            .position(|&c| c == b'\0')
            .ok_or(DocumentDBError::bad_value(
                "Message did not contain a string".to_string(),
            ))?;
    let s = ::std::str::from_utf8(&utf8_src[0..nul_range_end])
        .map_err(|_| DocumentDBError::bad_value("String was not a utf-8 string".to_string()))?;
    Ok((s, nul_range_end))
}

/// Parse an OP_MSG
async fn parse_msg<'a>(
    message: &'a RequestMessage,
    requires_response: &mut bool,
) -> Result<Request<'a>> {
    let reader = Cursor::new(message.request.as_slice());
    let msg: Message = Message::read_from_op_msg(reader, message.response_to)?;

    *requires_response = !msg._flags.contains(message::MessageFlags::MORE_TO_COME);
    match msg.sections.len() {
        0 => Err(DocumentDBError::bad_value(
            "Message had no sections".to_string(),
        )),
        1 => match &msg.sections[0] {
            MessageSection::Document(doc) => parse_cmd(doc, None).await,
            MessageSection::Sequence {
                size: _,
                _identifier: _,
                documents: _,
            } => Err(DocumentDBError::bad_value(
                "Expected the only section to be a document.".to_string(),
            )),
        },
        2 => match (&msg.sections[0], &msg.sections[1]) {
            (MessageSection::Document(doc), MessageSection::Document(extra)) => {
                parse_cmd(doc, Some(extra.as_bytes())).await
            }
            (
                MessageSection::Document(doc),
                MessageSection::Sequence {
                    documents: extras, ..
                },
            ) => parse_cmd(doc, Some(extras)).await,
            (MessageSection::Sequence { .. }, _) => Err(DocumentDBError::bad_value(
                "Expected first section to be a single document.".to_string(),
            )),
        },
        _ => Err(DocumentDBError::bad_value(
            "Expected at most two sections.".to_string(),
        )),
    }
}

/// Parse a command document
async fn parse_cmd<'a>(command: &'a RawDocument, extra: Option<&'a [u8]>) -> Result<Request<'a>> {
    if let Some(result) = command.into_iter().next() {
        let cmd_name = result?.0;

        let explain = command.get_bool("explain").unwrap_or(false);
        if explain {
            return Ok(Request::Raw(RequestType::Explain, command, extra));
        }

        let request_type = RequestType::from_str(cmd_name)?;
        Ok(Request::Raw(request_type, command, extra))
    } else {
        Err(DocumentDBError::bad_value(
            "Admin command received without a command.".to_string(),
        ))
    }
}

// TODO: Should not need to clone the documents and create a new RawDocumentBuf
async fn parse_insert<'a>(message: &'a RequestMessage) -> Result<Request<'a>> {
    let mut reader = Cursor::new(&message.request);
    let flags = reader.read_i32_le().await?;

    let (collection_path, endpos) = str_from_u8_nul_utf8(&reader.get_ref()[4..])?;

    // Skip the flags and the nul terminator
    let docs_slice = &reader.get_ref()[endpos + 5..];

    let (db, coll) = extract_database_and_collection_names(collection_path)?;

    Ok(Request::RawBuf(
        RequestType::Insert,
        rawdoc! {
            "insert": coll,
            "ordered": (flags & 1) == 0,
            "documents": read_documents(docs_slice)?,
            "$db": db,
        },
    ))
}

/// Reads a sequence of bson in bytes into an owned bson array
fn read_documents(bytes: &'_ [u8]) -> Result<RawArrayBuf> {
    let mut result = RawArrayBuf::new();
    let mut pos = 0;
    while pos < bytes.len() {
        let doc_size = i32::from_le_bytes(
            bytes[pos..pos + 4]
                .try_into()
                .expect("Slice of wrong length"),
        );
        result.push(RawDocumentBuf::from_bytes(
            bytes[pos..pos + (doc_size as usize)].to_vec(),
        )?);
        pos += doc_size as usize;
    }
    Ok(result)
}
