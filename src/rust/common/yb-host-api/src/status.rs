// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

//! A Rust mirror of the parts of `yb::Status` that cross the host boundary.

use std::fmt;

/// The subset of `yb::Status::Code` (src/yb/util/status_codes.h) used across the bridge. The
/// numeric values match the C++ enum so codes pass through unchanged.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
#[repr(i32)]
pub enum StatusCode {
    /// `Status::kNotFound`.
    NotFound = 1,
    /// `Status::kCorruption`.
    Corruption = 2,
    /// `Status::kNotSupported`.
    NotSupported = 3,
    /// `Status::kInvalidArgument`.
    InvalidArgument = 4,
    /// `Status::kIOError`.
    IoError = 5,
    /// `Status::kAlreadyPresent`.
    AlreadyPresent = 6,
    /// `Status::kRuntimeError`.
    RuntimeError = 7,
    /// `Status::kNetworkError`.
    NetworkError = 8,
    /// `Status::kIllegalState`.
    IllegalState = 9,
    /// `Status::kNotAuthorized`.
    NotAuthorized = 10,
    /// `Status::kAborted`.
    Aborted = 11,
    /// `Status::kRemoteError`.
    RemoteError = 12,
    /// `Status::kServiceUnavailable`.
    ServiceUnavailable = 13,
    /// `Status::kTimedOut`.
    TimedOut = 14,
    /// `Status::kUninitialized`.
    Uninitialized = 15,
    /// `Status::kConfigurationError`.
    ConfigurationError = 16,
    /// `Status::kIncomplete`.
    Incomplete = 17,
    /// `Status::kEndOfFile`.
    EndOfFile = 18,
    /// `Status::kInternalError`.
    InternalError = 21,
    /// `Status::kExpired`.
    Expired = 22,
    /// `Status::kLeaderNotReadyToServe`.
    LeaderNotReadyToServe = 23,
    /// `Status::kLeaderHasNoLease`.
    LeaderHasNoLease = 24,
    /// `Status::kTryAgain`.
    TryAgain = 25,
    /// `Status::kBusy`.
    Busy = 26,
}

impl StatusCode {
    /// Converts a C++ status code; unknown values become `InternalError`.
    pub fn from_i32(code: i32) -> StatusCode {
        use StatusCode::*;
        const ALL: [StatusCode; 24] = [
            NotFound,
            Corruption,
            NotSupported,
            InvalidArgument,
            IoError,
            AlreadyPresent,
            RuntimeError,
            NetworkError,
            IllegalState,
            NotAuthorized,
            Aborted,
            RemoteError,
            ServiceUnavailable,
            TimedOut,
            Uninitialized,
            ConfigurationError,
            Incomplete,
            EndOfFile,
            InternalError,
            Expired,
            LeaderNotReadyToServe,
            LeaderHasNoLease,
            TryAgain,
            Busy,
        ];
        ALL.into_iter()
            .find(|c| *c as i32 == code)
            .unwrap_or(InternalError)
    }
}

/// A non-OK status. `Ok` is expressed as `Result::Ok`, as with `yb::Result`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Status {
    /// The error code.
    pub code: StatusCode,
    /// The human-readable message.
    pub message: String,
}

impl Status {
    /// Creates a status with the given code and message.
    pub fn new(code: StatusCode, message: impl Into<String>) -> Status {
        Status {
            code,
            message: message.into(),
        }
    }
}

impl fmt::Display for Status {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}: {}", self.code, self.message)
    }
}

impl std::error::Error for Status {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn codes_round_trip() {
        for code in [StatusCode::NotFound, StatusCode::Busy, StatusCode::TryAgain] {
            assert_eq!(StatusCode::from_i32(code as i32), code);
        }
        assert_eq!(StatusCode::from_i32(0), StatusCode::InternalError);
        assert_eq!(StatusCode::from_i32(19), StatusCode::InternalError);
        assert_eq!(StatusCode::TryAgain as i32, 25);
    }
}
