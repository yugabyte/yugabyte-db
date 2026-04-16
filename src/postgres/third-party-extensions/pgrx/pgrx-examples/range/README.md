Examples for using pgrx' `Range<T>` support.

pgrx supports the Postgres `int4range`, `int8range`, `numrange`, `daterange`, `tsrange`, and `tstzrange` types, safely
mapped to `pgrx::Range<T>` where `T` is any of `i32`, `i64`, `Numeric<P, S>`, `AnyNumeric`, `Date`, `Timestamp`, and `TimestampWithTimeZone`.