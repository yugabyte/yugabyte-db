-- ----------------------------------------------------------------
-- Tests for Metadata Functions.
-- ----------------------------------------------------------------

SELECT hll_set_output_version(1);

SELECT hll_schema_version(NULL);
SELECT hll_schema_version(E'\\x108b7f');
SELECT hll_schema_version(E'\\x118b7f');
SELECT hll_schema_version(hll_empty(10,4,32,0));
SELECT hll_schema_version(E'\\x128b498895a3f5af28cafe');
SELECT hll_schema_version(E'\\x138b400061');
SELECT hll_schema_version(E'\\x14857f0840008001000020000008042000062884120021');

SELECT hll_type(NULL);
SELECT hll_type(E'\\x108b7f');
SELECT hll_type(E'\\x118b7f');
SELECT hll_type(hll_empty(10,4,32,0));
SELECT hll_type(E'\\x128b498895a3f5af28cafe');
SELECT hll_type(E'\\x138b400061');
SELECT hll_type(E'\\x14857f0840008001000020000008042000062884120021');

SELECT hll_log2m(NULL);
SELECT hll_log2m(E'\\x108b7f');
SELECT hll_log2m(E'\\x118b7f');
SELECT hll_log2m(hll_empty(10,4,32,0));
SELECT hll_log2m(E'\\x128b498895a3f5af28cafe');
SELECT hll_log2m(E'\\x138b400061');
SELECT hll_log2m(E'\\x14857f0840008001000020000008042000062884120021');

SELECT hll_regwidth(NULL);
SELECT hll_regwidth(E'\\x108b7f');
SELECT hll_regwidth(E'\\x118b7f');
SELECT hll_regwidth(hll_empty(10,4,32,0));
SELECT hll_regwidth(E'\\x128b498895a3f5af28cafe');
SELECT hll_regwidth(E'\\x138b400061');
SELECT hll_regwidth(E'\\x14857f0840008001000020000008042000062884120021');

SELECT hll_expthresh(NULL);
SELECT hll_expthresh(E'\\x108b7f');
SELECT hll_expthresh(E'\\x118b7f');
SELECT hll_expthresh(hll_empty(10,4,32,0));
SELECT hll_expthresh(E'\\x128b498895a3f5af28cafe');
SELECT hll_expthresh(E'\\x138b400061');
SELECT hll_expthresh(E'\\x14857f0840008001000020000008042000062884120021');

SELECT hll_sparseon(NULL);
SELECT hll_sparseon(E'\\x108b7f');
SELECT hll_sparseon(E'\\x118b7f');
SELECT hll_sparseon(hll_empty(10,4,32,0));
SELECT hll_sparseon(E'\\x128b498895a3f5af28cafe');
SELECT hll_sparseon(E'\\x138b400061');
SELECT hll_sparseon(E'\\x14857f0840008001000020000008042000062884120021');
