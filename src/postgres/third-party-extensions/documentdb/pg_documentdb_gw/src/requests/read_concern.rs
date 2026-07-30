/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/requests/read_concern.rs
 *
 *-------------------------------------------------------------------------
 */

use std::str::FromStr;

#[derive(Debug, Default, PartialEq)]
pub enum ReadConcern {
    /// Read concern is not specified.
    #[default]
    Unspecified,

    /// Read data as defined on the node, account for Mongo chunk migration
    /// in Mongo sharded cluster. Same as Available for unsharded clusters.
    Local,

    /// Read data as defined on the node.
    Available,

    /// Read with majority quorum.  
    Majority,

    /// Read with majority quorum such that all writes before time T are
    /// readable before the start of the operation.
    Linearizable,

    /// Read snapshot of data.
    Snapshot,
}

impl FromStr for ReadConcern {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_lowercase().as_str() {
            "" | "unspecified" => Ok(ReadConcern::Unspecified),
            "local" => Ok(ReadConcern::Local),
            "available" => Ok(ReadConcern::Available),
            "majority" => Ok(ReadConcern::Majority),
            "linearizable" => Ok(ReadConcern::Linearizable),
            "snapshot" => Ok(ReadConcern::Snapshot),
            _ => Err(()),
        }
    }
}
