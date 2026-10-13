//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use crate::{FromDatum, IntoDatum, PgMemoryContexts, pg_sys};
use core::fmt::Write;
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, ReturnsError, ReturnsRef, SqlMappingRef, SqlTranslatable,
};
use std::ops::{Deref, DerefMut};

const UUID_BYTES_LEN: usize = 16;
pub type UuidBytes = [u8; UUID_BYTES_LEN];

/// A Universally Unique Identifier (`UUID`) from PostgreSQL
#[derive(Clone, Copy, Eq, Hash, Ord, PartialEq, PartialOrd, Debug)]
#[repr(transparent)]
pub struct Uuid(UuidBytes);

impl IntoDatum for Uuid {
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        let ptr = unsafe {
            // SAFETY:  CurrentMemoryContext is always valid
            PgMemoryContexts::CurrentMemoryContext.palloc_slice::<u8>(UUID_BYTES_LEN)
        };
        ptr.clone_from_slice(&self.0);

        Some(ptr.as_ptr().into())
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        pg_sys::UUIDOID
    }
}

impl FromDatum for Uuid {
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _typoid: pg_sys::Oid,
    ) -> Option<Uuid> {
        if is_null {
            None
        } else {
            let bytes =
                std::slice::from_raw_parts(datum.cast_mut_ptr::<u8>() as *const u8, UUID_BYTES_LEN);
            Uuid::from_slice(bytes).ok()
        }
    }
}

enum UuidFormatCase {
    Lowercase,
    Uppercase,
}

impl Uuid {
    pub fn from_bytes(b: UuidBytes) -> Self {
        Uuid(b)
    }

    pub const fn as_bytes(&self) -> &UuidBytes {
        &self.0
    }

    pub fn from_slice(b: &[u8]) -> Result<Uuid, String> {
        let len = b.len();

        if len != UUID_BYTES_LEN {
            Err(format!("Expected UUID to be {UUID_BYTES_LEN} bytes, got {len}"))?;
        }

        let mut bytes = [0; UUID_BYTES_LEN];
        bytes.copy_from_slice(b);
        Ok(Uuid::from_bytes(bytes))
    }

    fn format(&self, f: &mut std::fmt::Formatter<'_>, case: UuidFormatCase) -> std::fmt::Result {
        let hyphenated = f.sign_minus();
        for (i, b) in self.0.iter().enumerate() {
            if hyphenated && (i == 4 || i == 6 || i == 8 || i == 10) {
                f.write_char('-')?;
            }
            match case {
                UuidFormatCase::Lowercase => write!(f, "{b:02x}")?,
                UuidFormatCase::Uppercase => write!(f, "{b:02X}")?,
            };
        }
        Ok(())
    }
}

impl Deref for Uuid {
    type Target = UuidBytes;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for Uuid {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl std::fmt::Display for Uuid {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:-x}")
    }
}

impl std::fmt::LowerHex for Uuid {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        self.format(f, UuidFormatCase::Lowercase)
    }
}

impl std::fmt::UpperHex for Uuid {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        self.format(f, UuidFormatCase::Uppercase)
    }
}

unsafe impl SqlTranslatable for Uuid {
    const TYPE_IDENT: &'static str = crate::pgrx_resolved_type!(Uuid);
    const TYPE_ORIGIN: pgrx_sql_entity_graph::metadata::TypeOrigin =
        pgrx_sql_entity_graph::metadata::TypeOrigin::External;
    const ARGUMENT_SQL: Result<SqlMappingRef, ArgumentError> = Ok(SqlMappingRef::literal("uuid"));
    const RETURN_SQL: Result<ReturnsRef, ReturnsError> =
        Ok(ReturnsRef::One(SqlMappingRef::literal("uuid")));
}
