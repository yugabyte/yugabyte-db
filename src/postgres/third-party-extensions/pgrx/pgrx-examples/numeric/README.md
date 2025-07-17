This demonstrates some examples of using pgrx' `NUMERIC` support.

There are two numeric types, `AnyNumeric` and `Numeric<P, S>`.  For both types, all numeric operations such
as construction and math operations are delegated to Postgres.  It is not pgrx' goal to implement a Postgres-compatible,
pure Rust, arbitrary precision number library.

## AnyNumeric

`AnyNumeric` is a general numeric type that can represent any value that a default Postgres `NUMERIC` (without a 
precision or scale) can.  As values that Rust can generate, it can hold anything between `i128::MIN` and `u128::MAX`, 
including the full range of `f32` and `f64`.

This type is mutable in that the various Rust math "XxxAssign" traits are implemented (ie, `AddAssign`).  

Conversion trait implementations:

- `From<{integer}> for AnyNumeric`
- `TryFrom<f32/64> for AnyNumeric`
- `TryFrom<&str> for AnyNumeric`
- `TryFrom<&CStr> for AnyNumeric` (b/c reasons)
- `FromStr for AnyNumeric`

For conversion from `AnyNumeric` to a primitive, `TryFrom` is implemented for all the primitive types.

`Display` and `Debug` are also implemented.

`AnyNumeric` also implements the following Rust math traits: Neg, Add, Sub, Mul, Div, Rem, AddAssign, SubAssign, MulAssign, 
DivAssign, and RemAssign.

One can try to "rescale" an `AnyNumeric`  to a specific `Numeric<P, S>` via the `::rescale<P, S>()` function.

## Numeric<P, S>

`Numeric<P, S>` keeps its precision and scale values with it as part of the Rust type system, which ensures that
the Rust compiler can assist you in ensuring operations don't change either value unexpectedly.

That said, performing math operations on a `Numeric<P, S>` (such as `a + b`) automatically convert to an
`AnyNumeric`, even if the P and S values are the same on both sides of the operator.

Conversion trait implementations:

- `TryFrom<{integer}> for Numeric<P, S>`
- `TryFrom<{f32/64}> for Numeric<P, S>`
- `TryFrom<&str> for Numeric<P, S>`
- `TryFrom<&CStr> for Numeric<P, S>` (b/c reasons)
- `FromStr for Numeric<P, S>`

One can try to "rescale" a `Numeric<P, S>` to a different precision and/or scale via the `::rescale<P, S>()` 
function.

## Interesting Notes

### To Infinity and... oh, seriously?

Postgres versions less than v14 don't know how to represent -/+Infinity as a numeric.  pgrx accounts for this
through some `#[cfg]` feature flags and the `Error::ConversionNotSupported` error variant.  :(  We also try 
to optimize checks for -/+Infinity on lesser Postgres versions just to avoid lots of conversion overhead.

If it weren't for this, we could support `From<f32/f64> for AnyNumeric` instead of `TryFrom`.

### Precision and Scale Slider Bars

Postgres says the scale of a numeric can be between [-1000..1000], but the SQL standard requires it to be between 
[0..precision].  It's the latter that pgrx essentially adopts.  pgrx uses a `u32` for the scale, so you can put any
value larger than precision in there and rustc won't catch it, but Postgres eventually will.

### Performance Notes

As it stands right now, performance isn't particularly fast.  pgrx has to delegate all operations on a numeric value,
including the construction of one, to Postgres, and this requires all sorts of overhead.  As it stands today, pgrx
wants numeric support to be correct, and using Postgres for all of it is the best way to ensure that.

