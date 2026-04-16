/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/configuration/version.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::RawArrayBuf;

pub enum Version {
    FourTwo,
    Five,
    Six,
    Seven,
    Eight,
}

impl Version {
    pub fn parse(val: &str) -> Option<Version> {
        match val {
            "4.2" => Some(Version::FourTwo),
            "5.0" => Some(Version::Five),
            "6.0" => Some(Version::Six),
            "7.0" => Some(Version::Seven),
            "8.0" => Some(Version::Eight),
            _ => None,
        }
    }

    pub fn as_str(&self) -> &str {
        match self {
            Version::FourTwo => "4.2.0",
            Version::Five => "5.0.0",
            Version::Six => "6.0.0",
            Version::Seven => "7.0.0",
            Version::Eight => "8.0.0",
        }
    }

    pub fn as_array(&self) -> [i32; 4] {
        match self {
            Version::FourTwo => [4, 2, 0, 0],
            Version::Five => [5, 0, 0, 0],
            Version::Six => [6, 0, 0, 0],
            Version::Seven => [7, 0, 0, 0],
            Version::Eight => [8, 0, 0, 0],
        }
    }

    pub fn as_bson_array(&self) -> RawArrayBuf {
        let mut array = RawArrayBuf::new();
        let versions = self.as_array();
        for v in versions {
            array.push(v)
        }
        array
    }

    pub fn max_wire_protocol(&self) -> i32 {
        match self {
            Version::FourTwo => 8,
            Version::Five => 13,
            Version::Six => 17,
            Version::Seven => 21,
            Version::Eight => 25,
        }
    }
}
