//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
/*!
Code for interfacing with the layout in memory (and on disk?) of various data types within Postgres.
This is not a mature module yet so prefer to avoid exposing the contents to public callers of pgrx,
as this is error-prone stuff to tamper with. It is easy to corrupt the database if an error is made.
Yes, even though its main block of code duplicates htup::DatumWithTypeInfo.

Technically, they can always use CFFI to corrupt the database if they know how to use the C API,
but that's why we should keep the conversation between the programmer and their own `unsafe`.
We don't want programmers trusting our implementation farther than we have solidly evaluated it.
Some may be better off extending the pgrx bindings themselves, doing their own layout checks, etc.
When PGRX is a bit more mature in its soundness, and we better understand what our callers expect,
then we may want to offer more help.
*/
#![allow(dead_code)]
use crate::pg_sys::{self, TYPALIGN_CHAR, TYPALIGN_DOUBLE, TYPALIGN_INT, TYPALIGN_SHORT};
use core::mem;

/// Postgres type information, corresponds to part of a row in pg_type
/// This layout describes T, not &T, even if passbyval: false, which would mean the datum array is effectively `&[&T]`
#[derive(Clone, Copy, Debug)]
pub(crate) struct Layout {
    // We could add more fields to this if we are curious enough, as the function we call pulls an entire row
    pub align: Align, // typalign
    pub size: Size,   // typlen
    pub pass: PassBy, // typbyval
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum PassBy {
    Ref,
    Value,
}

impl Layout {
    /**
    Give an Oid and get its Layout.

    # Panics

    May elog if the tuple for the type in question cannot be acquired.
    This should almost never happen in practice (alloc error?).
    */
    pub(crate) fn lookup_oid(oid: pg_sys::Oid) -> Layout {
        let (mut typalign, mut typlen, mut passbyval) = Default::default();
        // Postgres doesn't document any safety conditions. It probably is a safe function in the Rust sense.
        unsafe { pg_sys::get_typlenbyvalalign(oid, &mut typlen, &mut passbyval, &mut typalign) };
        Layout {
            align: Align::try_from(typalign).unwrap(),
            size: Size::try_from(typlen).unwrap(),
            pass: if passbyval { PassBy::Value } else { PassBy::Ref },
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) enum Align {
    Byte,
    Short,
    Int,
    Double,
}

impl TryFrom<libc::c_char> for Align {
    type Error = ();

    fn try_from(cchar: libc::c_char) -> Result<Align, ()> {
        match cchar as u8 {
            TYPALIGN_CHAR => Ok(Align::Byte),
            TYPALIGN_SHORT => Ok(Align::Short),
            TYPALIGN_INT => Ok(Align::Int),
            TYPALIGN_DOUBLE => Ok(Align::Double),
            _ => Err(()),
        }
    }
}

impl Align {
    pub(crate) fn as_usize(self) -> usize {
        match self {
            Align::Byte => mem::align_of::<libc::c_char>(),
            Align::Short => mem::align_of::<libc::c_short>(),
            Align::Int => mem::align_of::<libc::c_int>(),
            Align::Double => mem::align_of::<libc::c_double>(),
        }
    }

    pub(crate) fn as_typalign(self) -> libc::c_char {
        (match self {
            Align::Byte => TYPALIGN_CHAR,
            Align::Short => TYPALIGN_SHORT,
            Align::Int => TYPALIGN_INT,
            Align::Double => TYPALIGN_DOUBLE,
        }) as _
    }

    #[inline]
    pub(crate) fn pad(self, size: usize) -> usize {
        let align = self.as_usize();
        (size + (align - 1)) & !(align - 1)
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(crate) enum Size {
    CStr,
    Varlena,
    Fixed(u16),
}

impl TryFrom<i16> for Size {
    type Error = ();
    fn try_from(int2: i16) -> Result<Size, ()> {
        match int2 {
            -2 => Ok(Size::CStr),
            -1 => Ok(Size::Varlena),
            v @ 0.. => Ok(Size::Fixed(v as u16)),
            _ => Err(()),
        }
    }
}

impl Size {
    pub(crate) fn as_typlen(&self) -> i16 {
        match self {
            Self::CStr => -2,
            Self::Varlena => -1,
            Self::Fixed(v) => *v as _,
        }
    }
}
