If the filename starts with a "cumulative_union" prefix, then it's the
standard cumulative union format we've been using (cardinality,
multiset, union_cardinality, union_multiset) in which the
union_multiset is an accumulator over the subsequent lines.

If the filename starts with a "cumulative_add" prefix, then it's a new
format (cardinality, raw_value, multiset) in which the "raw_value" is
added to the accumulator "multiset".

The cutoffs I'm assuming in this file are 256 for explicit to sparse,
and 850 for sparse to full. Log2m=11, registerWidth=5, as usual.

A brief summary of what each file tries to accomplish follows:

cumulative_add_comprehensive_promotion.csv

Cumulatively adds random values to an EMPTY multiset.

Format: cumulative add
Tests:
- EMPTY, EXPLICIT, SPARSE_PROBABILISTIC, PROBABILSTIC addition
- EMPTY to EXPLICIT promotion
- EXPLICIT to SPARSE_PROBABILISTIC promotion
- SPARSE_PROBABILISTIC to PROBABILISTIC promotion

cumulative_add_sparse_step.csv

Cumulatively sets successive registers to:

    <code>(registerIndex % probabilisticRegisterMaxValue) + 1</code>

by adding specifically constructed values to a SPARSE_PROBABILISTIC multiset.
Does not induce promotion.

Format: cumulative add
Tests:
- SPARSE_PROBABILISTIC addition (predictable)

cumulative_add_sparse_random.csv

Cumulatively sets random registers of a SPARSE_PROBABILISTIC multiset to
random values by adding random values. Does not induce promotion.

Format: cumulative add
Tests:
- SPARSE_PROBABILISTIC addition (random)

cumulative_union_explicit_promotion.csv

Unions an EMPTY accumulator with EXPLICIT multisets, each containing a
single random value.

Format: cumulative union
Tests:
- EMPTY U EXPLICIT
- EXPLICIT U EXPLICIT
- EXPLICIT to SPARSE_PROBABILISTIC promotion
- SPARSE_PROBABILISTIC U EXPLICIT

cumulative_union_sparse_promotion.csv

Unions an EMPTY accumulator with SPARSE_PROBABILISTIC multisets, each
having one register set.

Format: cumulative union
Tests:
- EMPTY U SPARSE_PROBABILISTIC
- SPARSE_PROBABILISTIC U SPARSE_PROBABILISTIC
- SPARSE_PROBABILISTIC promotion
- SPARSE_PROBABILISTIC U PROBABILISTIC

cumulative_union_explicit_explicit.csv

Unions an EMPTY accumulator with EXPLICIT multisets, each having a single
random value, twice in a row to verify that the set properties are
satisfied.

Format: cumulative union
Tests:
- EMPTY U EXPLICIT
- EXPLICIT U EXPLICIT

cumulative_union_sparse_sparse.csv

Unions an EMPTY accumulator with SPARSE_PROBABILISTIC multisets, each
having a single register set, twice in a row to verify that the set
properties are satisfied.

Format: cumulative union
Tests:
- EMPTY U SPARSE_PROBABILISTIC
- SPARSE_PROBABILISTIC U SPARSE_PROBABILISTIC

cumulative_union_probabilistic_probabilistic.csv

Unions an EMPTY accumulator with PROBABILISTIC multisets, each having
many registers set, twice in a row to verify that the set properties are
satisfied.

Format: cumulative union
Tests:
- EMPTY U PROBABILISTIC
- PROBABILISTIC U PROBABILISTIC

cumulative_union_comprehensive.csv

Unions an EMPTY accumulator with random multisets.

Format: cumulative union
Tests:
- hopefully all union possibilities
