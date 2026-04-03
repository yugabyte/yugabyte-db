/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * tests/common/validation_utils.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::Document;
use mongodb::Database;

#[allow(dead_code)]
pub async fn execute_command_and_validate_error(
    db: &Database,
    command: Document,
    expected_error_code: i32,
    expected_error_message: &str,
) {
    let result = db.run_command(command).await;
    match result {
        Err(e) => {
            if let mongodb::error::ErrorKind::Command(ref cmd_err) = *e.kind {
                assert_eq!(
                    cmd_err.code, expected_error_code,
                    "Expected error code {expected_error_code}, but got {}",
                    cmd_err.code
                );

                assert!(
                    cmd_err.message.contains(expected_error_message),
                    "Expected error message to contain '{expected_error_message}', but got '{}'",
                    cmd_err.message
                );
            } else {
                panic!("Expected CommandError but got different error type: {e:?}");
            }
        }
        Ok(_) => panic!("Expected error but command succeeded"),
    }
}
