/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/telemetry/client_info.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::raw::{RawDocument, RawDocumentBuf};
use std::fmt;

/// Represents metadata about a client connecting to the server.
///
/// This struct contains optional fields for application, driver, and operating system information:
/// - `application_name`: Name of the client application.
/// - `driver_name`: Name of the driver.
/// - `driver_version`: Version of the driver.
/// - `os_type`: Type of operating system (e.g., "Linux").
/// - `os_name`: Name of the operating system (e.g., "Ubuntu").
/// - `os_architecture`: Architecture of the operating system (e.g., "x86_64").
/// - `os_version`: Version of the operating system (e.g., "22.04").
///
/// All fields are optional and may be `None` if not present in the source document.
#[derive(Debug, Default, Clone)]
pub struct ClientInformation {
    application_name: Option<String>,
    driver_name: Option<String>,
    driver_version: Option<String>,
    os_type: Option<String>,
    os_name: Option<String>,
    os_architecture: Option<String>,
    os_version: Option<String>,
}

impl ClientInformation {
    pub fn parse_from_client_document(doc: &RawDocument) -> Self {
        fn get_str(doc: &RawDocument, subdoc: &str, field: &str) -> Option<String> {
            doc.get_document(subdoc)
                .ok()
                .and_then(|d| d.get_str(field).ok())
                .map(str::to_owned)
        }

        Self {
            application_name: get_str(doc, "application", "name"),
            driver_name: get_str(doc, "driver", "name"),
            driver_version: get_str(doc, "driver", "version"),
            os_type: get_str(doc, "os", "type"),
            os_name: get_str(doc, "os", "name"),
            os_architecture: get_str(doc, "os", "architecture"),
            os_version: get_str(doc, "os", "version"),
        }
    }

    #[inline]
    pub fn is_empty(&self) -> bool {
        self.application_name.is_none()
            && self.driver_name.is_none()
            && self.driver_version.is_none()
            && self.os_type.is_none()
            && self.os_name.is_none()
            && self.os_architecture.is_none()
            && self.os_version.is_none()
    }
}

impl fmt::Display for ClientInformation {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let client_info: [Option<&str>; 7] = [
            self.application_name.as_deref(),
            self.driver_name.as_deref(),
            self.driver_version.as_deref(),
            self.os_type.as_deref(),
            self.os_name.as_deref(),
            self.os_architecture.as_deref(),
            self.os_version.as_deref(),
        ];

        let client_info_in_string = serde_json::to_string(&client_info).map_err(|_| fmt::Error)?;
        formatter.write_str(&client_info_in_string)
    }
}

/// Parses optional MongoDB client metadata into a JSON string, with a safe fallback.
///
/// Given an optional `RawDocumentBuf` from the Mongo handshake (`client` field),
/// this function tries to extract the seven well-known fields
/// (application.name, driver.{name,version}, os.{type,name,architecture,version})
/// via `ClientInformation::parse_from_client_document(...)`.
///
/// - If parsing yields any fields → returns a **JSON array** string via `ClientInformation::to_string()`
/// - If **none** of the expected fields are present → logs an error and returns the
///   **entire client document** serialized to JSON (fallback).
/// - If `client_information` is `None`, or BSON→Document conversion fails during
///   fallback, returns an **empty string**.
///
/// # Parameters
/// - `client_information`: Optional raw BSON document containing the client metadata.
///
/// # Returns
/// A JSON string:
/// - Usually a 7-element array like:
///   `["csm","PyMongo|c","4.14.1","Linux","Linux","x86_64","5.14.0-427.70.1.el9_4.x86_64"]`
/// - Or the raw document as JSON if the expected schema is missing
/// - Or `""` (empty) if nothing is available.
pub fn parse_client_info(client_information: Option<&RawDocumentBuf>) -> String {
    match client_information {
        Some(client_information_ref) => {
            let raw_client_information = client_information_ref.as_ref();
            let parsed_client_information =
                ClientInformation::parse_from_client_document(raw_client_information);

            if parsed_client_information.is_empty() {
                // Fallback: stringify the whole client doc as JSON
                tracing::error!("Failed to parse client information. Client information didn't match expected schema, falling back to raw document");

                match client_information_ref.to_document() {
                    Ok(doc) => serde_json::to_string(&doc).unwrap_or_default(),
                    Err(_) => String::new(),
                }
            } else {
                parsed_client_information.to_string()
            }
        }
        None => String::new(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bson::doc;

    fn make_raw_doc(doc: bson::Document) -> RawDocumentBuf {
        RawDocumentBuf::from_document(&doc).expect("Failed to convert Document to RawDocumentBuf")
    }

    #[test]
    fn test_parse_client_info_all_fields() {
        let doc = doc! {
            "application": { "name": "MyApp" },
            "driver": { "name": "test-driver", "version": "1.2.3" },
            "os": {
                "type": "Linux",
                "name": "Ubuntu",
                "architecture": "x86_64",
                "version": "22.04"
            }
        };
        let raw = make_raw_doc(doc);
        let result = parse_client_info(Some(&raw));
        assert_eq!(
            result,
            serde_json::to_string(&[
                Some("MyApp"),
                Some("test-driver"),
                Some("1.2.3"),
                Some("Linux"),
                Some("Ubuntu"),
                Some("x86_64"),
                Some("22.04")
            ])
            .unwrap()
        );
    }

    #[test]
    fn test_parse_client_info_missing_fields() {
        let doc = doc! {
            "application": { "name": "MyApp" },
            "driver": { "name": "test-driver" },
            "os": { "type": "Linux" }
        };
        let raw = make_raw_doc(doc);
        let result = parse_client_info(Some(&raw));
        assert_eq!(
            result,
            serde_json::to_string(&[
                Some("MyApp"),
                Some("test-driver"),
                None,
                Some("Linux"),
                None,
                None,
                None
            ])
            .unwrap()
        );
    }

    #[test]
    fn test_parse_client_info_none() {
        let result = parse_client_info(None);
        assert_eq!(result, "");
    }

    #[test]
    fn test_parse_client_info_empty_document() {
        let doc = doc! {};
        let raw = make_raw_doc(doc);
        let result = parse_client_info(Some(&raw));
        assert_eq!(result, "{}");
    }

    #[test]
    fn test_parse_client_info_partial_os() {
        let doc = doc! {
            "os": { "name": "Ubuntu", "version": "22.04" }
        };
        let raw = make_raw_doc(doc);
        let result = parse_client_info(Some(&raw));
        assert_eq!(
            result,
            serde_json::to_string(&[None, None, None, None, Some("Ubuntu"), None, Some("22.04")])
                .unwrap()
        );
    }

    #[test]
    fn test_parse_client_info_with_unknown_schema() {
        let doc = doc! {
            "user_agent": "Mozilla/5.0 (compatible; Nmap Scripting Engine; https://nmap.org/book/nse.html)"
        };
        let raw = make_raw_doc(doc);
        let result = parse_client_info(Some(&raw));
        assert_eq!(
            result,
            "{\"user_agent\":\"Mozilla/5.0 (compatible; Nmap Scripting Engine; https://nmap.org/book/nse.html)\"}"
        );
    }
}
