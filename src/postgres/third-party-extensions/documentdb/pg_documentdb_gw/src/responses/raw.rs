/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/responses/raw.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::{RawDocument, RawDocumentBuf};

use crate::error::Result;

/// Response constructed by the gateway from a raw BSON document.
#[derive(Debug)]
pub struct RawResponse(pub RawDocumentBuf);

impl RawResponse {
    pub fn as_raw_document(&self) -> Result<&RawDocument> {
        Ok(&self.0)
    }
}
