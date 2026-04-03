/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/requests/read_preference.rs
 *
 *-------------------------------------------------------------------------
 */

use crate::error::{DocumentDBError, ErrorCode, Result};
use bson::RawDocument;
use std::str::FromStr;

pub struct ReadPreference {}

#[derive(PartialEq)]
pub enum ReadPreferenceMode {
    Primary,
    Secondary,
    PrimaryPreferred,
    SecondaryPreferred,
    Nearest,
}

impl FromStr for ReadPreferenceMode {
    type Err = DocumentDBError;

    fn from_str(s: &str) -> std::result::Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "primary" => Ok(ReadPreferenceMode::Primary),
            "secondary" => Ok(ReadPreferenceMode::Secondary),
            "primarypreferred" => Ok(ReadPreferenceMode::PrimaryPreferred),
            "secondarypreferred" => Ok(ReadPreferenceMode::SecondaryPreferred),
            "nearest" => Ok(ReadPreferenceMode::Nearest),
            unsupported => Err(DocumentDBError::documentdb_error(
                ErrorCode::FailedToParse,
                format!("Unsupported read preference mode '{unsupported}'"),
            )),
        }
    }
}

impl ReadPreference {
    pub fn parse(raw_document: Option<&RawDocument>) -> Result<()> {
        match raw_document {
            None => Err(DocumentDBError::documentdb_error(
                ErrorCode::FailedToParse,
                "'$readPreference' must be a document".to_string(),
            )),
            Some(doc) => {
                let mut read_preference_mode: Option<ReadPreferenceMode> = None;
                let mut max_staleness_seconds: Option<i32> = None;
                let mut hedge: Option<bool> = None;

                for entry in doc {
                    let (k, v) = entry?;
                    match k {
                        "mode" => {
                            if read_preference_mode.is_some() {
                                return Err(DocumentDBError::documentdb_error(
                                    ErrorCode::FailedToParse,
                                    "'mode' field is already specified".to_string(),
                                ));
                            }

                            let mode_str = v.as_str().ok_or_else(|| {
                                DocumentDBError::documentdb_error(
                                    ErrorCode::FailedToParse,
                                    "'mode' field must be a string".to_string(),
                                )
                            })?;

                            read_preference_mode = Some(ReadPreferenceMode::from_str(mode_str)?);
                        }
                        "maxStalenessSeconds" => {
                            if max_staleness_seconds.is_some() {
                                return Err(DocumentDBError::documentdb_error(
                                    ErrorCode::FailedToParse,
                                    "'maxStalenessSeconds' field is already specified".to_string(),
                                ));
                            }

                            let seconds = v.as_i32().ok_or_else(|| {
                                DocumentDBError::documentdb_error(
                                    ErrorCode::FailedToParse,
                                    "'maxStalenessSeconds' field must be an integer".to_string(),
                                )
                            })?;

                            if seconds < 0 {
                                return Err(DocumentDBError::documentdb_error(
                                    ErrorCode::FailedToParse,
                                    "'maxStalenessSeconds' field must be non-negative".to_string(),
                                ));
                            }

                            max_staleness_seconds = Some(seconds);
                        }
                        "hedge" => {
                            if hedge.is_some() {
                                return Err(DocumentDBError::documentdb_error(
                                    ErrorCode::FailedToParse,
                                    "'hedge' field is already specified".to_string(),
                                ));
                            }

                            let hedge_doc = v.as_document().ok_or_else(|| {
                                DocumentDBError::documentdb_error(
                                    ErrorCode::FailedToParse,
                                    "'hedge' field must be a document".to_string(),
                                )
                            })?;

                            // For simplicity, we only check for the presence of 'enabled' field
                            for hedge_entry in hedge_doc {
                                let (hedge_k, hedge_v) = hedge_entry?;
                                if hedge_k == "enabled" {
                                    hedge = Some(hedge_v.as_bool().ok_or_else(|| {
                                        DocumentDBError::documentdb_error(
                                            ErrorCode::FailedToParse,
                                            "'enabled' field in 'hedge' must be a boolean"
                                                .to_string(),
                                        )
                                    })?);
                                }
                            }
                        }
                        "tags" => {
                            return Err(DocumentDBError::documentdb_error(
                                ErrorCode::FailedToSatisfyReadPreference,
                                "no server available for query with specified tag set list"
                                    .to_string(),
                            ));
                        }
                        _ => {
                            continue;
                        }
                    }
                }

                if read_preference_mode.is_none() {
                    return Err(DocumentDBError::documentdb_error(
                        ErrorCode::FailedToParse,
                        "'mode' field is required".to_string(),
                    ));
                }

                let read_preference_mode = read_preference_mode.unwrap();

                if read_preference_mode == ReadPreferenceMode::Primary {
                    if max_staleness_seconds.is_some() {
                        return Err(DocumentDBError::documentdb_error(
                            ErrorCode::FailedToParse,
                            "mode 'primary' does not allow for 'maxStalenessSeconds'".to_string(),
                        ));
                    }

                    if hedge.is_some() {
                        return Err(DocumentDBError::documentdb_error(
                            ErrorCode::FailedToParse,
                            "mode 'primary' does not allow for 'hedge'".to_string(),
                        ));
                    }
                }

                if read_preference_mode == ReadPreferenceMode::Secondary {
                    return Err(DocumentDBError::documentdb_error(
                        ErrorCode::FailedToSatisfyReadPreference,
                        "no server available for query with ReadPreference secondary".to_string(),
                    ));
                }

                if hedge == Some(true) {
                    return Err(DocumentDBError::documentdb_error(
                        ErrorCode::BadValue,
                        "hedged reads are not supported".to_string(),
                    ));
                }

                Ok(())
            }
        }
    }
}
