//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
//! Utility functions for working with `pg_sys::RangeType` structs
use crate::datum::{
    AnyNumeric, Date, FromDatum, IntoDatum, Numeric, Timestamp, TimestampWithTimeZone,
};
use crate::pg_sys;
use core::fmt::{Display, Formatter};
use pgrx_sql_entity_graph::metadata::{
    ArgumentError, Returns, ReturnsError, SqlMapping, SqlTranslatable,
};
use std::ops::{Deref, DerefMut, RangeFrom, RangeInclusive, RangeTo, RangeToInclusive};

/// A Postgres Range's "lower" or "upper" value
#[derive(Debug, Clone, Hash, Eq, PartialEq)]
pub enum RangeBound<T> {
    Infinite,
    Inclusive(T),
    Exclusive(T),
}

impl<T> RangeBound<T>
where
    T: RangeSubType,
{
    /// Returns a reference to this [`RangeBound`]'s underlying value.
    ///
    /// If this [`RangeBound`] is [`RangeBound::Infinite`], then `None` is returned, otherwise
    /// `Some(&T)`
    #[inline]
    pub fn get(&self) -> Option<&T> {
        match self {
            RangeBound::Infinite => None,
            RangeBound::Inclusive(v) | RangeBound::Exclusive(v) => Some(v),
        }
    }

    /// Returns true if this [`RangeBound`] represents the `Infinite` variant
    #[inline]
    pub fn is_infinite(&self) -> bool {
        matches!(self, RangeBound::Infinite)
    }

    /// Returns true if this [`RangeBound`] represents the `Inclusive` variant
    #[inline]
    pub fn is_inclusive(&self) -> bool {
        matches!(self, RangeBound::Inclusive(_))
    }

    /// Returns true if this [`RangeBound`] represents the `Exclusive` variant
    #[inline]
    pub fn is_exclusive(&self) -> bool {
        matches!(self, RangeBound::Exclusive(_))
    }

    /// Convert this pgrx [`RangeBound`] into the equivalent Postgres [`pg_sys::RangeBound`].
    ///
    /// Note that the `lower` property is always set to false as a [`RangeBound`] doesn't know the
    /// end on which it's placed.
    fn into_pg(self) -> pg_sys::RangeBound {
        match self {
            RangeBound::Infinite => pg_sys::RangeBound {
                val: pg_sys::Datum::from(0),
                infinite: true,
                inclusive: false,
                lower: false,
            },
            RangeBound::Inclusive(v) => pg_sys::RangeBound {
                val: v.into_datum().unwrap(),
                infinite: false,
                inclusive: true,
                lower: false,
            },
            RangeBound::Exclusive(v) => pg_sys::RangeBound {
                val: v.into_datum().unwrap(),
                infinite: false,
                inclusive: false,
                lower: false,
            },
        }
    }

    /// Create a typed pgrx [`RangeBound`] from an arbitrary Postgres [`pg_sys::RangeBound`].
    ///
    /// # Safety
    ///
    /// This function is unsafe as it cannot guarantee that the `val` property, which is a
    /// [`pg_sys::Datum`], points to (or is) something correct for the generic type `T`.
    pub unsafe fn from_pg(range_bound: pg_sys::RangeBound) -> RangeBound<T> {
        if range_bound.infinite {
            RangeBound::Infinite
        } else if range_bound.inclusive {
            // SAFETY: caller has asserted that `val` is a proper Datum for `T`
            unsafe { RangeBound::Inclusive(T::from_datum(range_bound.val, false).unwrap()) }
        } else {
            // SAFETY: caller has asserted that `val` is a proper Datum for `T`
            unsafe { RangeBound::Exclusive(T::from_datum(range_bound.val, false).unwrap()) }
        }
    }
}

impl<T> From<T> for RangeBound<T>
where
    T: RangeSubType,
{
    #[inline]
    fn from(value: T) -> Self {
        RangeBound::Inclusive(value)
    }
}

impl<T> From<Option<T>> for RangeBound<T>
where
    T: RangeSubType,
{
    /// Conversion of an [`Option`] to a [`RangeBound`].  
    ///
    /// `Some` maps to the [`RangeBound::Inclusive`] variant and `None` maps to the
    /// [`RangeBound::Infinite`] value.
    #[inline]
    fn from(value: Option<T>) -> Self {
        match value {
            Some(value) => RangeBound::Inclusive(value),
            None => RangeBound::Infinite,
        }
    }
}

/// A safe deconstruction of a Postgres `pg_sys::RangeType` struct.
///
/// In spirit, Postgres ranges are not dissimilar from Rust ranges, however they are represented
/// quite differently.  You'll use a [`RangeBound`] for the lower and upper bounds of a Postgres
/// [`Range`].
///
/// Unlike Rust, Postgres also has the concept of an "empty" range.  Such ranges are constructed via
/// the [`Range::empty()`] function.  As such, expect the various direct accessor methods on [`Range`]
/// to return `Option<&RangeBound<T>>` or `Option<(RangeBound<T>, RangeBound<T>)>`.
///
/// pgrx provides [`From`] implementations for Rust's built-in range types for easy conversion into
/// a Postgres range.  For example:
///
/// ```rust,no_run
/// use pgrx::datum::Range;
/// let r: Range<i32> = (1..10).into();
/// ```
#[derive(Debug, Clone, Hash, Eq, PartialEq)]
pub struct Range<T: RangeSubType> {
    inner: Option<(RangeBound<T>, RangeBound<T>)>,
}

impl<T> Display for Range<T>
where
    T: RangeSubType + Display,
{
    /// Follows Postgres' format for displaying ranges
    #[rustfmt::skip]
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        match self.as_ref() {
            None => write!(f, "empty"),
            Some((RangeBound::Infinite, RangeBound::Infinite)) => write!(f, "(,)"),
            Some((RangeBound::Infinite, RangeBound::Inclusive(v))) => write!(f, "(,{v}]"),
            Some((RangeBound::Infinite, RangeBound::Exclusive(v))) => write!(f, "(,{v})"),

            Some((RangeBound::Inclusive(v), RangeBound::Infinite)) => write!(f, "[{v},)"),
            Some((RangeBound::Inclusive(lo), RangeBound::Inclusive(hi))) => write!(f, "[{lo},{hi}]"),
            Some((RangeBound::Inclusive(lo), RangeBound::Exclusive(hi))) => write!(f, "[{lo},{hi})"),

            Some((RangeBound::Exclusive(v), RangeBound::Infinite)) => write!(f, "({v},)"),
            Some((RangeBound::Exclusive(lo), RangeBound::Inclusive(hi))) => write!(f, "({lo},{hi}]"),
            Some((RangeBound::Exclusive(lo), RangeBound::Exclusive(hi))) => write!(f, "({lo},{hi})"),
        }
    }
}

impl<T> Range<T>
where
    T: RangeSubType,
{
    /// Builds a new [`Range`] with bounds.
    ///
    /// # Examples
    ///
    /// ```rust,no_run
    /// use pgrx::datum::{Range, RangeBound};
    /// let _ = Range::<i32>::new(1, 10);  // `(1..=10)`
    /// let _ = Range::<i32>::new(None, 10); // `(..=10)`
    /// let _ = Range::<i32>::new(1, None); // `(1..)`
    /// let _ = Range::<i32>::new(None, RangeBound::Exclusive(10)); // `(..10)`
    /// let _ = Range::<i32>::new(1, RangeBound::Exclusive(10)); // (`1..10)`
    /// let _ = Range::<i32>::new(None, None); // `(..)`
    /// let _ = Range::<i32>::new(RangeBound::Infinite, RangeBound::Infinite); // `(..)`
    #[inline]
    pub fn new<L, U>(lower: L, upper: U) -> Self
    where
        L: Into<RangeBound<T>>,
        U: Into<RangeBound<T>>,
    {
        Self { inner: Some((lower.into(), upper.into())) }
    }

    /// Builds an "empty" range
    ///
    /// Unlike Rust ranges (from `std::ops::`), Postgres ranges can be empty, meaning they don't
    /// represent any range of values.
    #[inline]
    pub fn empty() -> Self {
        Self { inner: None }
    }

    /// Builds an "infinite" range.  This is equivalent to Rust's [`std::ops::RangeFull`] (`(..)`).
    #[inline]
    pub fn infinite() -> Self {
        Self::new(RangeBound::Infinite, RangeBound::Infinite)
    }

    /// Returns the lower [`RangeBound`]
    #[inline]
    pub fn lower(&self) -> Option<&RangeBound<T>> {
        match &self.inner {
            Some((l, _)) => Some(l),
            None => None,
        }
    }

    /// Returns the upper [`RangeBound`]
    #[inline]
    pub fn upper(&self) -> Option<&RangeBound<T>> {
        match &self.inner {
            Some((_, u)) => Some(u),
            None => None,
        }
    }

    /// Returns 'true' if the range is "empty".
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.inner.is_none()
    }

    /// Returns `true` if the range is "infinite".  This is equivalent to Rust's [`std::ops::RangeFull`] (`(..)`)
    #[inline]
    pub fn is_infinite(&self) -> bool {
        matches!(
            (self.lower(), self.upper()),
            (Some(RangeBound::Infinite), Some(RangeBound::Infinite))
        )
    }

    /// Consumes `self` and returns the internal representation, which can be easily mapped or
    /// unwrapped.
    ///
    /// A return value of [`Option::None`] indicates that this range represents the "empty" range.
    #[inline]
    pub fn into_inner(self) -> Option<(RangeBound<T>, RangeBound<T>)> {
        self.inner
    }

    /// Takes the bounds out of this [`Range`] and converts self to represent the "empty" range.
    ///
    /// A return value of [`Option::None`] indicates that this range already represents the "empty" range.
    #[inline]
    pub fn take(&mut self) -> Option<(RangeBound<T>, RangeBound<T>)> {
        self.inner.take()
    }

    /// Replace the bounds of this [`Range`], returning the old bounds.  
    ///
    /// An [`Option::None`] will replace this with the "empty" range.
    #[inline]
    pub fn replace(
        &mut self,
        new: Option<(RangeBound<T>, RangeBound<T>)>,
    ) -> Option<(RangeBound<T>, RangeBound<T>)> {
        std::mem::replace(&mut self.inner, new)
    }
}

impl<T> Deref for Range<T>
where
    T: RangeSubType,
{
    type Target = Option<(RangeBound<T>, RangeBound<T>)>;

    #[inline]
    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

impl<T> DerefMut for Range<T>
where
    T: RangeSubType,
{
    #[inline]
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.inner
    }
}

impl<T> FromDatum for Range<T>
where
    T: RangeSubType,
{
    /// ## Safety
    /// function requires that
    /// - is_null is true OR datum represents a PG RangeType datum
    #[inline]
    unsafe fn from_polymorphic_datum(
        datum: pg_sys::Datum,
        is_null: bool,
        _: pg_sys::Oid,
    ) -> Option<Self>
    where
        Self: Sized,
    {
        if is_null || datum.is_null() {
            None
        } else {
            let ptr: *mut pg_sys::varlena = datum.cast_mut_ptr();
            // Datum should be non-null and point to PG RangeType
            let range_type =
                unsafe { pg_sys::pg_detoast_datum(datum.cast_mut_ptr()) as *mut pg_sys::RangeType };

            let mut lower_bound: pg_sys::RangeBound = Default::default();
            let mut upper_bound: pg_sys::RangeBound = Default::default();
            let mut is_empty = false;

            unsafe {
                // SAFETY: range.range_type came from PG, so assume its rangetypid is valid
                let typecache = pg_sys::lookup_type_cache(
                    (*(range_type)).rangetypid,
                    pg_sys::TYPECACHE_RANGE_INFO as i32,
                );

                // SAFETY: PG will deserialize into lower/upper RangeBounds and is_empty
                pg_sys::range_deserialize(
                    typecache,
                    range_type,
                    &mut lower_bound,
                    &mut upper_bound,
                    &mut is_empty,
                );

                let range = if is_empty {
                    // empty ranges cannot go through `RangeBound::from_pg()` as the bound's `.val`
                    // Datum will be zero, and for a pass-by-reference range type, that's an instant segfault
                    Range::empty()
                } else {
                    // SAFETY: The lower_bound/upper_bound RangeBound value's .val will be a valid Datum of the T type
                    // If the range is_empty or either bound is infinite then .val = (Datum) 0, which we handled above
                    let lower = RangeBound::from_pg(lower_bound);
                    let upper = RangeBound::from_pg(upper_bound);

                    Range { inner: Some((lower, upper)) }
                };

                if !std::ptr::eq(ptr, range_type.cast()) {
                    // SAFETY: range_type was allocated by Postgres in the call to
                    // pg_detoast_datum above, so we know it's a valid pointer and needs to be freed
                    pg_sys::pfree(range_type.cast());
                }

                Some(range)
            }
        }
    }
}

impl<T> IntoDatum for Range<T>
where
    T: RangeSubType,
{
    #[inline]
    fn into_datum(self) -> Option<pg_sys::Datum> {
        unsafe {
            // T must have a valid registered "Range" Type ex. int4 -> int4range,
            let typecache =
                pg_sys::lookup_type_cache(T::range_type_oid(), pg_sys::TYPECACHE_RANGE_INFO as i32);

            let is_empty = self.is_empty();
            let (mut lower_bound, mut upper_bound) = self.inner.map_or_else(
                || (pg_sys::RangeBound::default(), pg_sys::RangeBound::default()),
                |(l, u)| (l.into_pg(), u.into_pg()),
            );

            // the lower_bound is the lower
            lower_bound.lower = true;

            // PG will serialize these lower/upper RangeBounds to a *RangeType ptr/datum
            #[cfg(any(feature = "pg13", feature = "pg14", feature = "pg15"))]
            let range_type =
                pg_sys::make_range(typecache, &mut lower_bound, &mut upper_bound, is_empty);

            #[cfg(any(feature = "pg16", feature = "pg17"))]
            let range_type = pg_sys::make_range(
                typecache,
                &mut lower_bound,
                &mut upper_bound,
                is_empty,
                std::ptr::null_mut(),
            );

            // *RangeType into Datum
            Some(pg_sys::Datum::from(range_type))
        }
    }

    #[inline]
    fn type_oid() -> pg_sys::Oid {
        T::range_type_oid()
    }
}

impl<T> From<std::ops::Range<T>> for Range<T>
where
    T: RangeSubType,
{
    #[inline]
    fn from(value: std::ops::Range<T>) -> Self {
        Range::new(RangeBound::Inclusive(value.start), RangeBound::Exclusive(value.end))
    }
}

impl<T> From<std::ops::RangeFrom<T>> for Range<T>
where
    T: RangeSubType,
{
    #[inline]
    fn from(value: RangeFrom<T>) -> Self {
        Range::new(Some(value.start), None)
    }
}

impl<T> From<std::ops::RangeFull> for Range<T>
where
    T: RangeSubType,
{
    #[inline]
    fn from(_: std::ops::RangeFull) -> Self {
        Range::new(RangeBound::Infinite, RangeBound::Infinite)
    }
}

impl<T> From<std::ops::RangeInclusive<T>> for Range<T>
where
    T: RangeSubType,
{
    #[inline]
    fn from(value: RangeInclusive<T>) -> Self {
        Range::new(
            RangeBound::Inclusive(Clone::clone(value.start())),
            RangeBound::Inclusive(Clone::clone(value.end())),
        )
    }
}

impl<T> From<std::ops::RangeTo<T>> for Range<T>
where
    T: RangeSubType,
{
    #[inline]
    fn from(value: RangeTo<T>) -> Self {
        Range::new(RangeBound::Infinite, RangeBound::Exclusive(value.end))
    }
}

impl<T> From<std::ops::RangeToInclusive<T>> for Range<T>
where
    T: RangeSubType,
{
    #[inline]
    fn from(value: RangeToInclusive<T>) -> Self {
        Range::new(RangeBound::Infinite, RangeBound::Inclusive(value.end))
    }
}

/// This trait allows a struct to be a valid subtype for a RangeType
pub unsafe trait RangeSubType: Clone + FromDatum + IntoDatum {
    fn range_type_oid() -> pg_sys::Oid;
}

/// for int/int4range
unsafe impl RangeSubType for i32 {
    fn range_type_oid() -> pg_sys::Oid {
        pg_sys::INT4RANGEOID
    }
}

/// for bigint/int8range
unsafe impl RangeSubType for i64 {
    fn range_type_oid() -> pg_sys::Oid {
        pg_sys::INT8RANGEOID
    }
}

/// for numeric/numrange
unsafe impl RangeSubType for AnyNumeric {
    fn range_type_oid() -> pg_sys::Oid {
        pg_sys::NUMRANGEOID
    }
}

/// for numeric/numrange
unsafe impl<const P: u32, const S: u32> RangeSubType for Numeric<P, S> {
    fn range_type_oid() -> pg_sys::Oid {
        pg_sys::NUMRANGEOID
    }
}

/// for date/daterange
unsafe impl RangeSubType for Date {
    fn range_type_oid() -> pg_sys::Oid {
        pg_sys::DATERANGEOID
    }
}

/// for Timestamp/tsrange
unsafe impl RangeSubType for Timestamp {
    fn range_type_oid() -> pg_sys::Oid {
        pg_sys::TSRANGEOID
    }
}

/// for Timestamp With Time Zone/tstzrange
unsafe impl RangeSubType for TimestampWithTimeZone {
    fn range_type_oid() -> pg_sys::Oid {
        pg_sys::TSTZRANGEOID
    }
}

unsafe impl SqlTranslatable for Range<i32> {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("int4range"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("int4range")))
    }
}

unsafe impl SqlTranslatable for Range<i64> {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("int8range"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("int8range")))
    }
}

unsafe impl SqlTranslatable for Range<AnyNumeric> {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("numrange"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("numrange")))
    }
}

unsafe impl<const P: u32, const S: u32> SqlTranslatable for Range<Numeric<P, S>> {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("numrange"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("numrange")))
    }
}

unsafe impl SqlTranslatable for Range<Date> {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("daterange"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("daterange")))
    }
}

unsafe impl SqlTranslatable for Range<TimestampWithTimeZone> {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("tstzrange"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("tstzrange")))
    }
}

unsafe impl SqlTranslatable for Range<Timestamp> {
    fn argument_sql() -> Result<SqlMapping, ArgumentError> {
        Ok(SqlMapping::literal("tsrange"))
    }
    fn return_sql() -> Result<Returns, ReturnsError> {
        Ok(Returns::One(SqlMapping::literal("tsrange")))
    }
}
