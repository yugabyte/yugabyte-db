//! Traits and implementations of functionality relating to Postgres null
//! and nullable types, particularly in relation to arrays and other container
//! types which implement nullable (empty slot) behavior in a
//! "structure-of-arrays" manner

use bitvec::slice::BitSlice;
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use std::{borrow::Cow, fmt::Debug};

pub enum Nullable<T> {
    Valid(T),
    Null,
}

impl<T> Debug for Nullable<T>
where
    T: Debug,
{
    #[inline]
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Valid(value) => f.debug_tuple("Valid").field(value).finish(),
            Self::Null => write!(f, "Null"),
        }
    }
}

impl<T> Clone for Nullable<T>
where
    T: Clone,
{
    #[inline]
    fn clone(&self) -> Self {
        match self {
            Self::Valid(arg0) => Self::Valid(arg0.clone()),
            Self::Null => Self::Null,
        }
    }
}

impl<T> Copy for Nullable<T> where T: Copy {}

impl<T> PartialEq for Nullable<T>
where
    T: PartialEq,
{
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::Valid(l_value), Self::Valid(r_value)) => l_value == r_value,
            // In Postgres' model, null is not equal to null.
            // https://postgresql.org/docs/current/functions-comparison.html
            // As a result, Null equality is not reflexive, and so we cannot
            // implement Eq on it.
            _ => false,
        }
    }
}

impl<T> PartialOrd for Nullable<T>
where
    T: PartialOrd + PartialEq,
{
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        match (self, other) {
            (Nullable::Valid(a), Nullable::Valid(b)) => a.partial_cmp(b),
            // Null cannot be considered to have a correct ordering.
            (Nullable::Valid(_), Nullable::Null) => None,
            (Nullable::Null, Nullable::Valid(_)) => None,
            (Nullable::Null, Nullable::Null) => None,
        }
    }
}

impl<T> Nullable<T> {
    #[inline]
    pub fn into_option(self) -> Option<T> {
        match self {
            Nullable::Valid(val) => Some(val),
            Nullable::Null => None,
        }
    }
    #[inline]
    pub fn as_option(&self) -> Option<&T> {
        match self {
            Nullable::Valid(val) => Some(val),
            Nullable::Null => None,
        }
    }
    #[inline]
    pub fn as_option_mut(&mut self) -> Option<&mut T> {
        match self {
            Nullable::Valid(val) => Some(val),
            Nullable::Null => None,
        }
    }
    // Returns true if this is a valid value (non-null).
    #[inline]
    pub fn is_valid(&self) -> bool {
        match self {
            Nullable::Valid(_) => true,
            Nullable::Null => false,
        }
    }
    // Returns true if this is Null.
    #[inline]
    pub fn is_null(&self) -> bool {
        !self.is_valid()
    }
    /// Return `value` if this enum is `Valid(value)`, otherwise panics.
    #[inline]
    pub fn unwrap(self) -> T {
        self.into_option().unwrap()
    }
    /// Return `value` if this enum is `Valid(value)`, otherwise panics with
    /// the error message `msg`.
    #[inline]
    pub fn expect<'a, A>(self, msg: &'a A) -> T
    where
        &'a A: Into<Cow<'a, str>>,
    {
        self.into_option().unwrap_or_else(|| panic!("{}", msg.into().as_ref()))
    }
    /// Convert to a result, returning `err` if this enum is `Null`.
    #[inline]
    pub fn valid_or<E>(self, err: E) -> Result<T, E> {
        self.into_option().ok_or(err)
    }
    /// Maps the `T` value inside a `Nullable<T>::Valid` into the U of
    /// a `Nullable<U>::Valid` using the provided `func` argument,
    /// or returns `Null` if this enum is `Null`.
    #[inline]
    pub fn map<U, F>(self, func: F) -> Nullable<U>
    where
        F: FnOnce(T) -> U,
    {
        match self {
            Nullable::Valid(v) => Nullable::Valid(func(v)),
            Nullable::Null => Nullable::Null,
        }
    }
    /// Returns `Null` if this value is `Null`, or, if this enum is
    /// `Valid(value)`, it calls `func(value)` and returns `func`'s result.
    /// This is very similar to [`Nullable::map()`]. However, `map()`
    /// maps the `value` inside a `Valid(value)` to another `Valid(value)`,
    /// while `and_then()` can be used to reject a valid value and return
    /// null anyway.
    #[inline]
    pub fn and_then<U, F>(self, func: F) -> Nullable<U>
    where
        F: FnOnce(T) -> Nullable<U>,
    {
        match self {
            Nullable::Valid(v) => func(v),
            Nullable::Null => Nullable::Null,
        }
    }
}

impl<T> From<Option<T>> for Nullable<T> {
    #[inline]
    fn from(opt: Option<T>) -> Nullable<T> {
        match opt {
            Some(val) => Nullable::Valid(val),
            None => Nullable::Null,
        }
    }
}
impl<T> From<Nullable<T>> for Option<T> {
    #[inline]
    fn from(nullable: Nullable<T>) -> Option<T> {
        match nullable {
            Nullable::Valid(val) => Some(val),
            Nullable::Null => None,
        }
    }
}

/* A null-bitmap array of (from postgres' perspective) length 8 with 3 nulls
in it has an actual data array size of `size_of<Elem>() * 5` (give or
take padding), whereas the bool array nulls implementation will still
have an underlying data array of `size_of<Elem>() * 8` (give or take
padding) no matter how many nulls there are. */
// For the bitmap array, 1 is "valid", 0 is "null",
// for the bool array,  1 is "null", 0 is "valid".

/// Represents Postgres internal types which describe the layout of where
/// filled slots (equivalent to Some(T)) are and where nulls (equivalent
/// to None) are in the array.
/// Note that a NullLayout must also capture the length of the container it
/// refers to.
pub trait NullLayout<Idx>
where
    Idx: PartialEq + PartialOrd,
{
    /// Returns true if this container has any nulls, checked at run-time.
    /// # Performance
    /// Different implementors of this type might have wildly varying
    /// performance costs for checking this - for a null-bitslice-style
    /// container, it should complete very quickly, but for a
    /// null-boolean-array-style implementation, it could be O(n),
    /// where n is the length of the container.
    fn contains_nulls(&self) -> bool;

    fn count_nulls(&self) -> usize;

    /// Returns Some(true) if the element at `idx` is valid (non-null),
    /// Some(false) if the element at `idx` is null,
    /// or `None` if `idx` is out-of-bounds.
    fn is_valid(&self, idx: Idx) -> Option<bool>;

    /// Returns Some(true) if the element at idx is null,
    /// Some(false) if the element at `idx` is valid (non-null),
    /// or `None` if `idx` is out-of-bounds.
    fn is_null(&self, idx: Idx) -> Option<bool> {
        self.is_valid(idx).map(|v| !v)
    }
}

pub trait NullableContainer<'mcx, Idx, T>
where
    Idx: PartialEq + PartialOrd,
{
    type Layout: NullLayout<Idx>;

    fn get_layout(&'mcx self) -> &'mcx Self::Layout;

    /// For internal use - implement this over underlying data types
    /// Used to implement NullableIter.
    ///
    /// Get the Valid value from the underlying data index of `idx`,
    /// after figuring out bounds-checking and the correct raw idx
    /// for this element.
    /// This means that it does not take nulls into account, in the sense of
    /// what element "idx" refers to, so a skipping nullable container with
    /// elements `\[Valid(foo), Null, Null, Valid(bar)\]` will return
    /// `Valid(bar)` for `skipping.get_raw(1)`.
    unsafe fn get_raw(&'mcx self, idx: Idx) -> T;

    /// Total array length - doesn't represent the length of the backing array
    /// (as in, the thing accessed by get_raw) but instead represents the
    /// length of the array including skipped nulls.
    fn len(&'mcx self) -> usize;
}

/// Bitmap of null slots on a container.
/// For example, 00001001 would represent:
/// \[valid, valid, valid, valid, null, valid, valid, null\]
/// However, the underlying data buffer would be:
/// \[valid, valid, valid, valid, valid, valid\]
/// because of the skip behavior
pub struct BitSliceNulls<'a>(pub &'a BitSlice<u8>);

impl<'a> NullLayout<usize> for BitSliceNulls<'a> {
    /// Returns true if this container has any nulls, checked at run-time.
    /// # Performance
    /// This implementation should be very fast, since all it needs to do
    /// is check if at least one byte isn't `0b00000000u8`.
    #[inline]
    fn contains_nulls(&self) -> bool {
        self.0.not_all()
    }

    /// Returns Some(true) if the element at `idx` is valid (non-null),
    /// Some(false) if the element at `idx` is null,
    /// or `None` if `idx` is out-of-bounds.
    #[inline]
    fn is_valid(&self, idx: usize) -> Option<bool> {
        // For the bitmap array, 1 is "valid", 0 is "null",
        self.0.get(idx).map(|b| *b)
    }
    /// Returns Some(true) if the element at `idx` is null,
    /// Some(false) if the element at `idx` is valid (non-null),
    /// or `None` if `idx` is out-of-bounds.
    #[inline]
    fn is_null(&self, idx: usize) -> Option<bool> {
        self.0.get(idx).map(|b| !b)
    }

    #[inline]
    fn count_nulls(&self) -> usize {
        self.0.count_zeros()
    }
}

pub struct BoolSliceNulls<'a>(pub &'a [bool]);

impl<'a> NullLayout<usize> for BoolSliceNulls<'a> {
    /// Returns true if this container has any nulls, checked at run-time.
    /// # Performance
    /// This implementation requires iterating over one boolean per
    /// array entry, and so it has linear worst-case time complexity on the
    /// container's length.
    #[inline]
    fn contains_nulls(&self) -> bool {
        for value in self.0 {
            if *value {
                return true;
            }
        }
        false
    }

    /// Returns Some(true) if the element at `idx` is valid (non-null),
    /// Some(false) if the element at `idx` is null,
    /// or `None` if `idx` is out-of-bounds.
    #[inline]
    fn is_valid(&self, idx: usize) -> Option<bool> {
        // In a Postgres bool-slice implementation of a null layout,
        // 1 is "null", 0 is "valid"
        if idx < self.0.len() {
            // Invert from 0 being valid to 1 being valid.
            Some(!self.0[idx])
        } else {
            None
        }
    }

    /// Returns Some(true) if the element at `idx` is null,
    /// Some(false) if the element at `idx` is valid (non-null),
    /// or `None` if `idx` is out-of-bounds.
    #[inline]
    fn is_null(&self, idx: usize) -> Option<bool> {
        if idx < self.0.len() {
            Some(self.0[idx])
        } else {
            None
        }
    }

    #[inline]
    fn count_nulls(&self) -> usize {
        let mut count = 0;
        for elem in self.0 {
            count += *elem as usize;
        }
        count
    }
}

/// Strict i.e. no nulls.
/// Useful for using nullable primitives on non-null structures,
/// especially when this needs to be determined at runtime.
#[derive(PartialEq, Eq, PartialOrd, Ord, Hash, Copy, Clone)]
pub struct StrictNulls(pub usize);

impl NullLayout<usize> for StrictNulls {
    #[inline]
    fn contains_nulls(&self) -> bool {
        false
    }

    #[inline]
    fn is_valid(&self, idx: usize) -> Option<bool> {
        (idx < self.0).then_some(true)
    }

    #[inline]
    fn is_null(&self, idx: usize) -> Option<bool> {
        (idx < self.0).then_some(false)
    }

    #[inline]
    fn count_nulls(&self) -> usize {
        0
    }
}

pub struct MaybeStrictNulls<Inner: NullLayout<usize>> {
    pub inner: Option<Inner>,
}

impl<Inner> MaybeStrictNulls<Inner>
where
    Inner: NullLayout<usize>,
{
    #[inline]
    pub fn new(inner: Option<Inner>) -> Self {
        Self { inner }
    }
    #[inline]
    pub fn is_strict(&self) -> bool {
        self.inner.is_none()
    }
    #[inline]
    pub fn get_inner(&self) -> Option<&Inner> {
        self.inner.as_ref()
    }
}

impl<'mcx, Inner> NullLayout<usize> for MaybeStrictNulls<Inner>
where
    Inner: NullLayout<usize>,
{
    #[inline]
    fn contains_nulls(&self) -> bool {
        match self.inner.as_ref() {
            Some(inner) => inner.contains_nulls(),
            None => false,
        }
    }

    #[inline]
    fn count_nulls(&self) -> usize {
        match self.inner.as_ref() {
            Some(inner) => inner.count_nulls(),
            None => 0,
        }
    }

    #[inline]
    fn is_valid(&self, idx: usize) -> Option<bool> {
        match self.inner.as_ref() {
            Some(inner) => inner.is_valid(idx),
            None => Some(true),
        }
    }

    #[inline]
    fn is_null(&self, idx: usize) -> Option<bool> {
        match self.inner.as_ref() {
            Some(inner) => inner.is_null(idx),
            None => Some(false),
        }
    }
}

/// IntoIterator-like trait for Nullable elements.
pub trait IntoNullableIterator<T> {
    type Iter: Iterator<Item = Nullable<T>>;
    fn into_nullable_iter(self) -> Self::Iter;
}

unsafe impl<T> SqlTranslatable for Nullable<T>
where
    T: SqlTranslatable,
{
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        T::argument_sql()
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        T::return_sql()
    }
    fn optional() -> bool {
        true
    }
}
