# A partition is added for rows that a batched COPY has yet to load, while the
# COPY is stopped in its third batch by the same advisory lock gate as in
# yb.orig.batched_copy_concurrent_ddl. If COPY kept routing rows with the
# partition list of its first batch, those rows would be stored in the
# DEFAULT partition (GH #34963). No row may end up in the wrong partition.
#
# s1_copy_def_first sends its first row to the DEFAULT partition;
# s1_copy_def_later does not. Expected outcome, by permutation:
# 1. ATTACH waits for COPY. COPY then fails with "schema version mismatch",
#    where PostgreSQL loads every row and fails the ATTACH. An unbatched COPY
#    fails the same way.
# 2. ATTACH does not wait, as in PostgreSQL. Each batch rebuilds its partition
#    routing, so the batches after the ATTACH store their rows in pt3.
# 3. CREATE TABLE ... PARTITION OF waits for COPY, as in PostgreSQL, and then
#    fails on the rows COPY stored in the DEFAULT partition.
#
# Permutation 1 creates the DEFAULT partition with a fixed OID, so that the
# table id in its error is the same in every run. The other permutations use
# a new OID.

setup
{
  CREATE FUNCTION copy_gate(k int) RETURNS bool LANGUAGE plpgsql AS $$
    BEGIN
      IF k = 25 THEN
        PERFORM pg_advisory_lock(1);
        PERFORM pg_advisory_unlock(1);
      END IF;
      RETURN true;
    END $$;
  CREATE TABLE pt (k int PRIMARY KEY, b int, CONSTRAINT gate CHECK (copy_gate(k))) PARTITION BY RANGE (k);
  CREATE TABLE pt1 PARTITION OF pt FOR VALUES FROM (1) TO (1000);
  CREATE TABLE pt3 (k int PRIMARY KEY, b int, CONSTRAINT gate CHECK (copy_gate(k)));
}

teardown
{
  DROP TABLE pt; DROP TABLE IF EXISTS pt3; DROP TABLE IF EXISTS pt4; DROP FUNCTION copy_gate(int);
}

session s1
step s1_copy_def_first	{ COPY pt FROM PROGRAM $$echo 9000,1; seq -f "%g,1" 1 40; seq -f "%g,1" 2000 2009$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_def_later	{ COPY pt FROM PROGRAM $$seq -f "%g,1" 1 40; seq -f "%g,1" 2000 2009$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }

session s2
step def_fixed_oid	{ SET yb_enable_create_with_table_oid = on; CREATE TABLE pt_def PARTITION OF pt DEFAULT WITH (table_oid = 50003); }
step def			{ CREATE TABLE pt_def PARTITION OF pt DEFAULT; }
step attach			{ ALTER TABLE pt ATTACH PARTITION pt3 FOR VALUES FROM (2000) TO (3000); }
step create_part	{ CREATE TABLE pt4 PARTITION OF pt FOR VALUES FROM (2000) TO (3000); }
step where			{ SELECT tableoid::regclass::text AS part, count(*) FROM pt GROUP BY 1 ORDER BY 1; }

session s3
step s3_lock		{ SELECT pg_advisory_lock(1); }
step s3_unlock		{ SELECT pg_advisory_unlock(1); }

permutation def_fixed_oid s3_lock s1_copy_def_first attach s3_unlock where
permutation def s3_lock s1_copy_def_later attach s3_unlock where
permutation def s3_lock s1_copy_def_later create_part s3_unlock where
