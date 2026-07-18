-- YB COPY TEST

-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

-- Create table.
CREATE TABLE onek (
    unique1     int4,
    unique2     int4,
    two         int4,
    four        int4,
    ten         int4,
    twenty      int4,
    hundred     int4,
    thousand    int4,
    twothousand int4,
    fivethous   int4,
    tenthous    int4,
    odd         int4,
    even        int4,
    stringu1    name,
    stringu2    name,
    string4     name
);

--
-- Test non-txn COPY on regular table.
--
\set filename :abs_srcdir '/data/onek.data'
COPY onek FROM :'filename';

-- Verify data is there.
SELECT COUNT(*) FROM onek;

-- Clear the data.
TRUNCATE onek;

--
-- Test non-txn COPY on a table with an index.
--
CREATE INDEX ON onek(unique1);
\set filename :abs_srcdir '/data/onek.data'
COPY onek FROM :'filename';

-- Verify data is there.
SELECT COUNT(*) FROM onek;

-- Verify non-transactional copy on hash, range
-- and list partitioned tables.
CREATE TABLE onek_hash(
    unique1     int4,
    unique2     int4,
    two         int4,
    four        int4,
    ten         int4,
    twenty      int4,
    hundred     int4,
    thousand    int4,
    twothousand int4,
    fivethous   int4,
    tenthous    int4,
    odd         int4,
    even        int4,
    stringu1    name,
    stringu2    name,
    string4     name
) PARTITION BY HASH(unique1);

CREATE TABLE onek_hash0 PARTITION OF onek_hash FOR VALUES WITH (modulus 2, remainder 0);
CREATE TABLE onek_hash1 PARTITION OF onek_hash FOR VALUES WITH (modulus 2, remainder 1);

\set filename :abs_srcdir '/data/onek.data'
COPY onek_hash FROM :'filename';

SELECT COUNT(*) FROM onek_hash;

CREATE TABLE onek_range(
    unique1     int4,
    unique2     int4,
    two         int4,
    four        int4,
    ten         int4,
    twenty      int4,
    hundred     int4,
    thousand    int4,
    twothousand int4,
    fivethous   int4,
    tenthous    int4,
    odd         int4,
    even        int4,
    stringu1    name,
    stringu2    name,
    string4     name
) PARTITION BY RANGE(unique2);

CREATE TABLE onek_range0 PARTITION OF onek_range FOR VALUES FROM (0) TO (500);
CREATE TABLE onek_range1 PARTITION OF onek_range FOR VALUES FROM (500) TO (1000);

\set filename :abs_srcdir '/data/onek.data'
COPY onek_range FROM :'filename';

SELECT COUNT(*) FROM onek_range;

CREATE TABLE onek_list(
    unique1     int4,
    unique2     int4,
    two         int4,
    four        int4,
    ten         int4,
    twenty      int4,
    hundred     int4,
    thousand    int4,
    twothousand int4,
    fivethous   int4,
    tenthous    int4,
    odd         int4,
    even        int4,
    stringu1    name,
    stringu2    name,
    string4     name
) PARTITION BY LIST(two);

CREATE TABLE onek_list0 PARTITION OF onek_list FOR VALUES IN (0);
CREATE TABLE onek_list1 PARTITION OF onek_list FOR VALUES IN (1);

\set filename :abs_srcdir '/data/onek.data'
COPY onek_list FROM :'filename';

SELECT COUNT(*) FROM onek_list;
