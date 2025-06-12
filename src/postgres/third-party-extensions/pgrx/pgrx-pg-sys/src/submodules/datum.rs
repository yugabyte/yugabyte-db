//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
// Polyfill while #![feature(strict_provenance)] is unstable
use crate::NullableDatum;
use std::ptr::NonNull;

/// Postgres defines the "Datum" type as uintptr_t, so bindgen decides it is usize.
/// Normally, this would be fine, except Postgres uses it more like void*:
/// A pointer to anything that could mean anything, check your assumptions before using.
///
///
/// Accordingly, the "Datum" type from bindgen is not entirely correct, as
/// Rust's `usize` may match the size of `uintptr_t` but it is not quite the same.
/// The compiler would rather know which integers are integers and which are pointers.
/// As a result, Datum is now a wrapper around `*mut DatumBlob`.
/// This type need not be exported unless the details of the type idiom become important.
// This struct uses a Rust idiom invented before `extern type` was designed,
// but should probably be replaced when #![feature(extern_type)] stabilizes
#[repr(C)]
struct DatumBlob {
    _data: [u8; 0],
    _marker: core::marker::PhantomData<(*mut u8, core::marker::PhantomPinned)>,
}

/// Datum is an abstract value that is effectively a union of all scalar types
/// and all possible pointers in a Postgres context. That is, it is either
/// "pass-by-value" (if the value fits into the platform's `uintptr_t`) or
/// "pass-by-reference" (if it does not).
///
/// In Rust, it is best to treat this largely as a pointer while passing it around
/// for code that doesn't care about what the Datum "truly is".
/// If for some reason it is important to manipulate the address/value
/// without "knowing the type" of the Datum, cast to a pointer and use pointer methods.
///
/// Only create Datums from non-pointers when you know you want to pass a value, as
/// it is erroneous for `unsafe` code to dereference the address of "only a value" as a pointer.
/// It is still a "safe" operation to create such pointers: validity is asserted by dereferencing,
/// **or by creating a safe reference such as &T or &mut T**. Also be aware that the validity
/// of Datum's Copy is premised on the same implicit issues with pointers being Copy:
/// while any `&T` is live, other `*mut T` must not be used to write to that `&T`,
/// and `&mut T` implies no other `*mut T` even exists outside an `&mut T`'s borrowing ancestry.
/// It is thus of dubious soundness for Rust code to receive `*mut T`, create another `*mut T`,
/// cast the first to `&mut T`, and then later try to use the second `*mut T` to write.
/// It _is_ sound for Postgres itself to pass a copied pointer as a Datum to Rust code, then later
/// to mutate that data through its original pointer after Rust creates and releases a `&mut T`.
///
/// For all intents and purposes, Postgres counts as `unsafe` code that may be relying
/// on you communicating pointers correctly to it. Do not play games with your database.
#[repr(transparent)]
#[derive(Debug, Copy, Clone, PartialEq)]
pub struct Datum(*mut DatumBlob);

impl Datum {
    /// Assume the datum is a value and extract the bits from
    /// the memory address, interpreting them as an integer.
    #[inline]
    pub fn value(self) -> usize {
        sptr::Strict::addr(self.0)
    }

    #[inline]
    pub const fn null() -> Datum {
        Datum(core::ptr::null_mut())
    }

    /// True if the datum is equal to the null pointer.
    #[inline]
    pub fn is_null(self) -> bool {
        self.0.is_null()
    }

    /// Assume the datum is a pointer and cast it to point to T.
    /// It is recommended to explicitly use `datum.cast_mut_ptr::<T>()`.
    #[inline]
    pub fn cast_mut_ptr<T>(self) -> *mut T {
        self.0.cast()
    }
}

impl From<usize> for Datum {
    #[inline]
    fn from(val: usize) -> Datum {
        Datum(sptr::Strict::with_addr(NonNull::<DatumBlob>::dangling().as_ptr(), val))
    }
}

impl From<Datum> for usize {
    #[inline]
    fn from(val: Datum) -> usize {
        sptr::Strict::addr(val.0)
    }
}

impl From<isize> for Datum {
    #[inline]
    fn from(val: isize) -> Datum {
        Datum::from(val as usize)
    }
}

impl From<u8> for Datum {
    #[inline]
    fn from(val: u8) -> Datum {
        Datum::from(usize::from(val))
    }
}

impl From<u16> for Datum {
    #[inline]
    fn from(val: u16) -> Datum {
        Datum::from(usize::from(val))
    }
}

impl From<u32> for Datum {
    #[inline]
    fn from(val: u32) -> Datum {
        Datum::from(val as usize)
    }
}

impl From<u64> for Datum {
    #[inline]
    fn from(val: u64) -> Datum {
        if cfg!(target_pointer_width = "64") {
            Datum::from(val as usize)
        } else {
            unsafe {
                let ptr = crate::palloc(size_of::<u64>()) as *mut u64;
                *ptr = val;
                Datum::from(ptr)
            }
        }
    }
}

impl From<i8> for Datum {
    #[inline]
    fn from(val: i8) -> Datum {
        Datum::from(isize::from(val))
    }
}

impl From<i16> for Datum {
    #[inline]
    fn from(val: i16) -> Datum {
        Datum::from(isize::from(val))
    }
}

impl From<i32> for Datum {
    #[inline]
    fn from(val: i32) -> Datum {
        Datum::from(val as usize)
    }
}

impl From<i64> for Datum {
    #[inline]
    fn from(val: i64) -> Datum {
        if cfg!(target_pointer_width = "64") {
            Datum::from(val as usize)
        } else {
            unsafe {
                let ptr = crate::palloc(size_of::<i64>()) as *mut i64;
                *ptr = val;
                Datum::from(ptr)
            }
        }
    }
}

impl From<bool> for Datum {
    #[inline]
    fn from(val: bool) -> Datum {
        Datum::from(val as usize)
    }
}

impl<T> From<*mut T> for Datum {
    #[inline]
    fn from(val: *mut T) -> Datum {
        Datum(val.cast())
    }
}

impl<T> From<*const T> for Datum {
    #[inline]
    fn from(val: *const T) -> Datum {
        Datum(val as *mut _)
    }
}

impl<T> PartialEq<*mut T> for Datum {
    #[inline]
    fn eq(&self, other: &*mut T) -> bool {
        &self.0.cast() == other
    }
}

impl<T> PartialEq<Datum> for *mut T {
    #[inline]
    fn eq(&self, other: &Datum) -> bool {
        self == &other.0.cast()
    }
}

impl TryFrom<NullableDatum> for Datum {
    type Error = ();

    #[inline]
    fn try_from(nd: NullableDatum) -> Result<Datum, ()> {
        let NullableDatum { value, isnull } = nd;
        if isnull {
            Err(())
        } else {
            Ok(value)
        }
    }
}

impl From<NullableDatum> for Option<Datum> {
    #[inline]
    fn from(nd: NullableDatum) -> Option<Datum> {
        Datum::try_from(nd).ok()
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn roundtrip_integers() {
        let val = i64::MAX;
        let datum = Datum::from(val);
        assert_eq!(datum.value() as i64, val);

        let val = isize::MAX;
        let datum = Datum::from(val);
        assert_eq!(datum.value() as isize, val);

        let val = i64::MIN;
        let datum = Datum::from(val);
        assert_eq!(datum.value() as i64, val);

        let val = isize::MIN;
        let datum = Datum::from(val);
        assert_eq!(datum.value() as isize, val);

        let val = u64::MAX;
        let datum = Datum::from(val);
        assert_eq!(datum.value() as u64, val);

        let val = usize::MAX;
        let datum = Datum::from(val);
        assert_eq!(datum.value(), val);
    }
}
