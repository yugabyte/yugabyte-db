//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use std::ops::{
    Add, AddAssign, Deref, Div, DivAssign, Mul, MulAssign, Neg, Rem, RemAssign, Sub, SubAssign,
};

use super::call_numeric_func;
use crate::{pg_sys, AnyNumeric, Numeric};

impl<const P: u32, const S: u32> Deref for Numeric<P, S> {
    type Target = AnyNumeric;

    #[inline]
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

macro_rules! anynumeric_math_op {
    ($opname:ident, $trait_fnname:ident, $pg_func:ident) => {
        impl $opname<AnyNumeric> for AnyNumeric {
            type Output = AnyNumeric;

            #[inline]
            fn $trait_fnname(self, rhs: AnyNumeric) -> Self::Output {
                call_numeric_func(pg_sys::$pg_func, &[self.as_datum(), rhs.as_datum()])
            }
        }

        /// [`AnyNumeric`] on the left, [`Numeric`] on the right.  Results in a new [`AnyNumeric`]
        impl<const P: u32, const S: u32> $opname<AnyNumeric> for Numeric<P, S> {
            type Output = AnyNumeric;

            #[inline]
            fn $trait_fnname(self, rhs: AnyNumeric) -> Self::Output {
                call_numeric_func(pg_sys::$pg_func, &[self.as_datum(), rhs.as_datum()])
            }
        }
    };
}

anynumeric_math_op!(Add, add, numeric_add);
anynumeric_math_op!(Sub, sub, numeric_sub);
anynumeric_math_op!(Mul, mul, numeric_mul);
anynumeric_math_op!(Div, div, numeric_div);
anynumeric_math_op!(Rem, rem, numeric_mod);

macro_rules! numeric_math_op {
    ($opname:ident, $trait_fnname:ident, $pg_func:ident) => {
        /// Doing this operation on two [`Numeric`] instances results in a new [`AnyNumeric`].
        impl<const P: u32, const S: u32, const Q: u32, const T: u32> $opname<Numeric<Q, T>>
            for Numeric<P, S>
        {
            type Output = AnyNumeric;

            #[inline]
            fn $trait_fnname(self, rhs: Numeric<Q, T>) -> Self::Output {
                call_numeric_func(pg_sys::$pg_func, &[self.as_datum(), rhs.as_datum()])
            }
        }

        /// [`Numeric`] on the left, [`AnyNumeric`] on the right.  Results in a new [`AnyNumeric`]
        impl<const Q: u32, const T: u32> $opname<Numeric<Q, T>> for AnyNumeric {
            type Output = AnyNumeric;

            #[inline]
            fn $trait_fnname(self, rhs: Numeric<Q, T>) -> Self::Output {
                call_numeric_func(pg_sys::$pg_func, &[self.as_datum(), rhs.as_datum()])
            }
        }
    };
}

numeric_math_op!(Add, add, numeric_add);
numeric_math_op!(Sub, sub, numeric_sub);
numeric_math_op!(Mul, mul, numeric_mul);
numeric_math_op!(Div, div, numeric_div);
numeric_math_op!(Rem, rem, numeric_mod);

impl Neg for AnyNumeric {
    type Output = AnyNumeric;

    #[inline]
    fn neg(self) -> Self::Output {
        call_numeric_func(pg_sys::numeric_uminus, &[self.as_datum()])
    }
}

impl<const P: u32, const S: u32> Neg for Numeric<P, S> {
    type Output = Numeric<P, S>;

    #[inline]
    fn neg(self) -> Self::Output {
        Numeric(call_numeric_func(pg_sys::numeric_uminus, &[self.as_datum()]))
    }
}

macro_rules! anynumeric_assign_op_from_anynumeric {
    ($opname:ident, $trait_fname:ident, $pg_func:ident) => {
        impl $opname<AnyNumeric> for AnyNumeric {
            fn $trait_fname(&mut self, rhs: AnyNumeric) {
                *self = call_numeric_func(pg_sys::$pg_func, &[self.as_datum(), rhs.as_datum()]);
            }
        }
    };
}

anynumeric_assign_op_from_anynumeric!(AddAssign, add_assign, numeric_add);
anynumeric_assign_op_from_anynumeric!(SubAssign, sub_assign, numeric_sub);
anynumeric_assign_op_from_anynumeric!(MulAssign, mul_assign, numeric_mul);
anynumeric_assign_op_from_anynumeric!(DivAssign, div_assign, numeric_div);
anynumeric_assign_op_from_anynumeric!(RemAssign, rem_assign, numeric_mod);

macro_rules! anynumeric_assign_op_from_primitive {
    ($opname:ident, $trait_fname:ident, $ty:ty, $op:tt) => {
        impl $opname<$ty> for AnyNumeric {
            #[inline]
            fn $trait_fname(&mut self, rhs: $ty) {
                *self = self.clone() $op AnyNumeric::from(rhs);
            }
        }
    }
}

anynumeric_assign_op_from_primitive!(AddAssign, add_assign, i128, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, isize, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, i64, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, i32, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, i16, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, i8, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, u128, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, usize, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, u64, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, u32, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, u16, +);
anynumeric_assign_op_from_primitive!(AddAssign, add_assign, u8, +);

anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, i128, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, isize, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, i64, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, i32, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, i16, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, i8, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, u128, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, usize, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, u64, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, u32, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, u16, -);
anynumeric_assign_op_from_primitive!(SubAssign, sub_assign, u8, -);

anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, i128, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, isize, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, i64, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, i32, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, i16, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, i8, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, u128, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, usize, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, u64, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, u32, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, u16, *);
anynumeric_assign_op_from_primitive!(MulAssign, mul_assign, u8, *);

anynumeric_assign_op_from_primitive!(DivAssign, div_assign, i128, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, isize, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, i64, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, i32, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, i16, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, i8, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, u128, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, usize, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, u64, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, u32, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, u16, /);
anynumeric_assign_op_from_primitive!(DivAssign, div_assign, u8, /);

anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, i128, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, isize, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, i64, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, i32, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, i16, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, i8, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, u128, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, usize, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, u64, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, u32, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, u16, %);
anynumeric_assign_op_from_primitive!(RemAssign, rem_assign, u8, %);

macro_rules! anynumeric_assign_op_from_float {
    ($opname:ident, $trait_fname:ident, $ty:ty, $op:tt) => {
        impl $opname<$ty> for AnyNumeric {
            #[inline]
            fn $trait_fname(&mut self, rhs: $ty) {
                // these versions of Postgres could produce an error when unwrapping a try_from(float)
                #[cfg(feature = "pg13")]
                {
                    *self = self.clone() $op AnyNumeric::try_from(rhs).unwrap();
                }

                // these versions won't, so we use .unwrap_unchecked()
                #[cfg(any(feature = "pg14", feature = "pg15", feature = "pg16", feature = "pg17"))]
                {
                    unsafe {
                        *self = self.clone() $op AnyNumeric::try_from(rhs).unwrap_unchecked();
                    }
                }
            }
        }
    };
}

anynumeric_assign_op_from_float!(AddAssign, add_assign, f32, +);
anynumeric_assign_op_from_float!(SubAssign, sub_assign, f32, -);
anynumeric_assign_op_from_float!(MulAssign, mul_assign, f32, *);
anynumeric_assign_op_from_float!(DivAssign, div_assign, f32, /);
anynumeric_assign_op_from_float!(RemAssign, rem_assign, f32, %);

anynumeric_assign_op_from_float!(AddAssign, add_assign, f64, +);
anynumeric_assign_op_from_float!(SubAssign, sub_assign, f64, -);
anynumeric_assign_op_from_float!(MulAssign, mul_assign, f64, *);
anynumeric_assign_op_from_float!(DivAssign, div_assign, f64, /);
anynumeric_assign_op_from_float!(RemAssign, rem_assign, f64, %);

macro_rules! primitive_math_op {
    ($opname:ident, $primitive:ty, $trait_fnname:ident, $pg_func:ident) => {
        /// [`AnyNumeric`] on the left, rust primitive on the right.  Results in an [`AnyNumeric`]
        impl $opname<$primitive> for AnyNumeric {
            type Output = AnyNumeric;

            #[inline]
            fn $trait_fnname(self, rhs: $primitive) -> Self::Output {
                call_numeric_func(
                    pg_sys::$pg_func,
                    &[self.as_datum(), AnyNumeric::try_from(rhs).unwrap().as_datum()],
                )
            }
        }

        /// Rust primitive on the left, [`AnyNumeric`] on the right.  Results in an [`AnyNumeric`]
        impl $opname<AnyNumeric> for $primitive {
            type Output = AnyNumeric;

            #[inline]
            fn $trait_fnname(self, rhs: AnyNumeric) -> Self::Output {
                call_numeric_func(
                    pg_sys::$pg_func,
                    &[AnyNumeric::try_from(self).unwrap().as_datum(), rhs.as_datum()],
                )
            }
        }

        /// [`Numeric`] on the left, rust primitive on the right.  Results in an [`AnyNumeric`]
        impl<const P: u32, const S: u32> $opname<$primitive> for Numeric<P, S> {
            type Output = AnyNumeric;

            #[inline]
            fn $trait_fnname(self, rhs: $primitive) -> Self::Output {
                call_numeric_func(
                    pg_sys::$pg_func,
                    &[self.as_datum(), AnyNumeric::try_from(rhs).unwrap().as_datum()],
                )
            }
        }

        /// Rust primitive on the left, [`Numeric`] on the right.  Results in an [`AnyNumeric`]
        impl<const P: u32, const S: u32> $opname<Numeric<P, S>> for $primitive {
            type Output = AnyNumeric;

            #[inline]
            fn $trait_fnname(self, rhs: Numeric<P, S>) -> Self::Output {
                call_numeric_func(
                    pg_sys::$pg_func,
                    &[rhs.as_datum(), AnyNumeric::try_from(self).unwrap().as_datum()],
                )
            }
        }
    };
}

primitive_math_op!(Add, u8, add, numeric_add);
primitive_math_op!(Add, u16, add, numeric_add);
primitive_math_op!(Add, u32, add, numeric_add);
primitive_math_op!(Add, u64, add, numeric_add);
primitive_math_op!(Add, u128, add, numeric_add);
primitive_math_op!(Add, usize, add, numeric_add);
primitive_math_op!(Add, i8, add, numeric_add);
primitive_math_op!(Add, i16, add, numeric_add);
primitive_math_op!(Add, i32, add, numeric_add);
primitive_math_op!(Add, i64, add, numeric_add);
primitive_math_op!(Add, i128, add, numeric_add);
primitive_math_op!(Add, isize, add, numeric_add);
primitive_math_op!(Add, f32, add, numeric_add);
primitive_math_op!(Add, f64, add, numeric_add);

primitive_math_op!(Sub, u8, sub, numeric_sub);
primitive_math_op!(Sub, u16, sub, numeric_sub);
primitive_math_op!(Sub, u32, sub, numeric_sub);
primitive_math_op!(Sub, u64, sub, numeric_sub);
primitive_math_op!(Sub, u128, sub, numeric_sub);
primitive_math_op!(Sub, usize, sub, numeric_sub);
primitive_math_op!(Sub, i8, sub, numeric_sub);
primitive_math_op!(Sub, i16, sub, numeric_sub);
primitive_math_op!(Sub, i32, sub, numeric_sub);
primitive_math_op!(Sub, i64, sub, numeric_sub);
primitive_math_op!(Sub, i128, sub, numeric_sub);
primitive_math_op!(Sub, isize, sub, numeric_sub);
primitive_math_op!(Sub, f32, sub, numeric_sub);
primitive_math_op!(Sub, f64, sub, numeric_sub);

primitive_math_op!(Mul, u8, mul, numeric_mul);
primitive_math_op!(Mul, u16, mul, numeric_mul);
primitive_math_op!(Mul, u32, mul, numeric_mul);
primitive_math_op!(Mul, u64, mul, numeric_mul);
primitive_math_op!(Mul, u128, mul, numeric_mul);
primitive_math_op!(Mul, usize, mul, numeric_mul);
primitive_math_op!(Mul, i8, mul, numeric_mul);
primitive_math_op!(Mul, i16, mul, numeric_mul);
primitive_math_op!(Mul, i32, mul, numeric_mul);
primitive_math_op!(Mul, i64, mul, numeric_mul);
primitive_math_op!(Mul, i128, mul, numeric_mul);
primitive_math_op!(Mul, isize, mul, numeric_mul);
primitive_math_op!(Mul, f32, mul, numeric_mul);
primitive_math_op!(Mul, f64, mul, numeric_mul);

primitive_math_op!(Div, u8, div, numeric_div);
primitive_math_op!(Div, u16, div, numeric_div);
primitive_math_op!(Div, u32, div, numeric_div);
primitive_math_op!(Div, u64, div, numeric_div);
primitive_math_op!(Div, u128, div, numeric_div);
primitive_math_op!(Div, usize, div, numeric_div);
primitive_math_op!(Div, i8, div, numeric_div);
primitive_math_op!(Div, i16, div, numeric_div);
primitive_math_op!(Div, i32, div, numeric_div);
primitive_math_op!(Div, i64, div, numeric_div);
primitive_math_op!(Div, i128, div, numeric_div);
primitive_math_op!(Div, isize, div, numeric_div);
primitive_math_op!(Div, f32, div, numeric_div);
primitive_math_op!(Div, f64, div, numeric_div);

primitive_math_op!(Rem, u8, rem, numeric_mod);
primitive_math_op!(Rem, u16, rem, numeric_mod);
primitive_math_op!(Rem, u32, rem, numeric_mod);
primitive_math_op!(Rem, u64, rem, numeric_mod);
primitive_math_op!(Rem, u128, rem, numeric_mod);
primitive_math_op!(Rem, usize, rem, numeric_mod);
primitive_math_op!(Rem, i8, rem, numeric_mod);
primitive_math_op!(Rem, i16, rem, numeric_mod);
primitive_math_op!(Rem, i32, rem, numeric_mod);
primitive_math_op!(Rem, i64, rem, numeric_mod);
primitive_math_op!(Rem, i128, rem, numeric_mod);
primitive_math_op!(Rem, isize, rem, numeric_mod);
primitive_math_op!(Rem, f32, rem, numeric_mod);
primitive_math_op!(Rem, f64, rem, numeric_mod);
