//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use std::fmt::{self, Debug, Display, Formatter};

pub type MultiXactId = TransactionId;

/// An `xid` type from PostgreSQL
#[repr(transparent)]
#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
#[derive(serde::Deserialize, serde::Serialize)]
pub struct TransactionId(u32);

impl TransactionId {
    pub const INVALID: Self = Self(0);
    pub const BOOTSTRAP: Self = Self(1);
    pub const FROZEN: Self = Self(2);
    pub const FIRST_NORMAL: Self = Self(3);
    pub const MAX: Self = Self(u32::MAX);

    pub const fn from_inner(xid: u32) -> Self {
        Self(xid)
    }

    pub const fn into_inner(self) -> u32 {
        self.0
    }
}

impl Default for TransactionId {
    fn default() -> Self {
        Self::INVALID
    }
}

impl From<u32> for TransactionId {
    #[inline]
    fn from(xid: u32) -> Self {
        Self::from_inner(xid)
    }
}

impl From<TransactionId> for u32 {
    #[inline]
    fn from(xid: TransactionId) -> Self {
        xid.into_inner()
    }
}

impl From<TransactionId> for crate::Datum {
    fn from(xid: TransactionId) -> Self {
        xid.into_inner().into()
    }
}

unsafe impl SqlTranslatable for TransactionId {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("xid"))
    }

    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("xid")))
    }
}

impl Debug for TransactionId {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        Debug::fmt(&self.0, f)
    }
}

impl Display for TransactionId {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        Display::fmt(&self.0, f)
    }
}
