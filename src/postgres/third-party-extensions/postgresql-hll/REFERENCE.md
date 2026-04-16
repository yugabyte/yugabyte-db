Types
=====

`hll`
-----

The HLL data structure. Casts between `bytea` and `hll` are supported, should you choose to generate the contents of the `hll` outside of the normal means. See `STORAGE.markdown`.

`SELECT hll_cardinality(E'\\xDEADBEEF');`

OR

`SELECT hll_cardinality(E'\\xDEADBEEF'::hll);`

`hll_hashval`
-------------

Represents a hashed data value. Backed by a 64-bit integer (`int8in`). Typically only output by the `hll_hash_*` functions. `bigint` and `integer` can both be cast to it if you want to skip hashing those values with the typical `123::hll_hashval`. Note that an `integer` that is cast will also be cast, with sign extension, to a 64-bit integer.

Defaults Functions
==================

All defaults for the `hll_empty` and `hll_add_agg` functions are in the C file, not in the SQL control file. The defaults can be changed (per connection) with:

`SELECT hll_set_defaults(log2m, regwidth, expthresh, sparseon);`

This returns a 4-tuple with the values of the prior defaults in the same order as the arguments.

Basic Operational Functions
===========================

`hll_cardinality(hll)` - returns `NULL` if the `hll`'s type is `UNDEFINED`. Returns a `double precision` floating point value otherwise. The prefix operator `#` may be used as shorthand.

`hll_union(hll, hll)` - returns the union (as an `hll`) of two `hll`s. The infix operator `||` may be used as shorthand.

`hll_add(hll, hll_hashval)` - adds the `hll_hashval` to the `hll` and returns the new representation of the `hll`. The infix operator `||` may be used as shorthand, like  `hll || hll_hashval` or `hll_hashval || hll`.

`hll_empty([log2m[, regwidth[, expthresh[, sparseon]]]])` - returns an empty `hll` of the specified parameters. Any number of the parameters may be left blank and the default values will be used. See `hll_set_defaults`.

`hll_eq(hll, hll)` - returns a `boolean` indicating whether the two `hll`s match when their binary representations are compared. The infix operator `=` may be used as shorthand.

`hll_ne(hll, hll)` - returns a `boolean` indicating whether the two `hll`s do not match when their binary representations are compared. The infix operator `<>` may be used as shorthand.

`hll_union_agg(hll)` - aggregate function for `hll`s that unions the `hll`s in the input set and returns the `hll` representing their union.

`hll_add_agg(hll_hashval, [log2m[, regwidth[, expthresh[, sparseon]]]])` - aggregate function for `hll_hashval`s that inserts each element in the input set into an `hll` whose parameters are specified by the four optional arguments. If any of the four optional arguments are not specified, the defaults set with `hll_set_defaults()` will be used. Returns the `hll` representing the input set.

Debugging Functions
===================

`hll_print(hll)` - pretty-prints the `hll` in a different way based on its type.

Metadata Functions
==================

`hll_schema_version(hll)` - returns the schema version value (integer) of the `hll`.

`hll_type(hll)` - returns the schema version-specific type value (integer) of the `hll`. See the [storage specification (v1.0.0)](https://github.com/aggregateknowledge/hll-storage-spec/blob/v1.0.0/STORAGE.md) for more details.

`hll_regwidth(hll)` - returns the register bit-width (integer) of the `hll`.

`hll_log2m(hll)` - returns the log-base-2 of the number of registers of the `hll`. If the `hll` is not of type `FULL` or `SPARSE` it returns the `log2m` value which would be used if the `hll` were promoted.

`hll_expthresh(hll)` - returns a 2-tuple of the specified and effective `EXPLICIT` promotion cutoffs for the `hll`. The specified cutoff and the effective cutoff will be the same unless `expthresh` has been set to 'auto' (`-1`). In that case the specified value will be `-1` and the effective value will be the implementation-dependent number of explicit values that will be stored before an `EXPLICIT` `hll` is promoted.

`hll_sparseon(hll)` - returns `1` if the `SPARSE` representation is enabled for the `hll`, and `0` otherwise.

Override Functions
==================

`SELECT hll_set_output_version(int)` - sets the output schema version to the specified value and returns the previous value. The value set only applies within your connection.

`SELECT hll_set_max_sparse(int)` - sets the maximum number of materialized registers in a `SPARSE` `hll` before it is promoted to a `FULL` `hll` for all `hll`s that have `sparseon` enabled. If `-1` is provided, the cutoff will be determined based on storage efficiency and is implementation-dependent. If `0` is provided, the `SPARSE` representation will be skipped and `FULL` will be used instead. If any value greater than zero or less than 2^`log2m` is provided, promotion will occur after that number of materialized registers. If any value greater than or equal to 2^`log2m` is used, promotion to `FULL` will never occur.


Hash Functions
==============

All values inserted into an `hll` should be hashed, and as a result `hll_add` and `hll_add_agg` only accept `hll_hashval`s. We do not recommend hashing floating point values raw as their bit-representation is [not well-suited to hashing](http://stackoverflow.com/questions/7403210/hashing-floating-point-values). Consider converting them to a reproducible, comparable binary representation (such as the [IEEE 754-2008 interchange format](http://en.wikipedia.org/wiki/IEEE_754-2008)) before hashing.

All the `hll_hash_*` functions below accept a seed value, which defaults to `0`. We discourage negative seeds in order to maintain hashed-value compatibility with the [Google Guava implementation of the 128-bit version of Murmur3](http://guava-libraries.googlecode.com/git/guava/src/com/google/common/hash/Murmur3_128HashFunction.java). Negative hash seeds will produce a warning when used.

`hll_hash_boolean(boolean)` - hashes the `boolean` value into a `hll_hashval`.

`hll_hash_smallint(smallint)` - hashes the `smallint` value into a `hll_hashval`.

`hll_hash_integer(integer)` - hashes the `integer` value into a `hll_hashval`.

`hll_hash_bigint(bigint)` - hashes the `bigint` value into a `hll_hashval`.

`hll_hash_bytea(bytea)` - hashes the `bytea` value into a `hll_hashval`.

`hll_hash_text(text)` - hashes the `text` value into a `hll_hashval`.

`hll_hash_any(scalar)` - hashes any PG data type by resolving the type dynamically and dispatching to the correct function for that type. This is significantly slower than the type-specific hash functions, and should only be used when the input type is not known beforehand.