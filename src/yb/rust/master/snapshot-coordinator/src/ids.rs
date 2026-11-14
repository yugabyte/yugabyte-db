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

//! Snapshot, schedule and restoration ids (`TxnSnapshotId` and friends): 16 bytes of a UUID.

use rand::RngCore;

/// The size of an id.
pub const ID_SIZE: usize = 16;

/// `GenerateRandom`: a version-4 UUID.
pub fn random_id() -> Vec<u8> {
    let mut bytes = [0u8; ID_SIZE];
    rand::rng().fill_bytes(&mut bytes);
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    bytes.to_vec()
}

/// `TryFullyDecode`: the id, or None (the nil id) if `bytes` is not 16 bytes or all zero.
pub fn try_decode(bytes: &[u8]) -> Option<&[u8]> {
    (bytes.len() == ID_SIZE && bytes.iter().any(|b| *b != 0)).then_some(bytes)
}

/// `Uuid::ToString`: the dashed lowercase form.
pub fn to_string(bytes: &[u8]) -> String {
    if bytes.len() != ID_SIZE {
        return String::from_utf8_lossy(bytes).into_owned();
    }
    let hex: String = bytes.iter().map(|b| format!("{b:02x}")).collect();
    format!(
        "{}-{}-{}-{}-{}",
        &hex[0..8],
        &hex[8..12],
        &hex[12..16],
        &hex[16..20],
        &hex[20..32]
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ids() {
        let id = random_id();
        assert_eq!(id.len(), ID_SIZE);
        assert_eq!(id[6] >> 4, 4);
        assert_eq!(try_decode(&id), Some(id.as_slice()));
        assert_eq!(try_decode(&[0; 16]), None);
        assert_eq!(try_decode(b"short"), None);
        assert_eq!(
            to_string(&[
                0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                0x07, 0x08
            ]),
            "12345678-9abc-def0-0102-030405060708"
        );
    }
}
