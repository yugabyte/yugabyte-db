/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/responses/mod.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::{rawdoc, Document, RawDocument};

use crate::error::Result;
use crate::protocol::OK_SUCCEEDED;

pub mod constant;
mod error;
mod pg;
mod raw;
pub mod writer;

pub use error::CommandError;
pub use pg::PgResponse;
pub use raw::RawResponse;

#[derive(Debug)]
pub enum Response {
    Raw(RawResponse),
    Pg(PgResponse),
}

impl Response {
    pub fn as_raw_document(&self) -> Result<&RawDocument> {
        match self {
            Response::Pg(pg) => pg.as_raw_document(),
            Response::Raw(raw) => raw.as_raw_document(),
        }
    }

    pub fn as_json(&self) -> Result<Document> {
        Ok(Document::try_from(self.as_raw_document()?)?)
    }

    pub fn ok() -> Self {
        Response::Raw(RawResponse(rawdoc! {
            "ok":OK_SUCCEEDED,
        }))
    }
}
