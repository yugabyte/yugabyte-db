# A partition is added for a range of rows that a batched COPY has yet to
# load, while the COPY is stopped in its third batch. The table has a DEFAULT
# partition, which is where those rows would go if COPY kept routing them with
# the partition list of its first batch (GH #34963).
#
# When COPY has put a row in the DEFAULT partition, ATTACH PARTITION must wait
# for COPY. When COPY has not touched the DEFAULT partition, ATTACH PARTITION
# does not wait, as in PostgreSQL, and the rows of the new range then fail the
# DEFAULT partition's constraint; CREATE TABLE ... PARTITION OF waits, and then
# fails on the rows COPY put in the DEFAULT partition.
#
# In the first permutation the COPY fails with "schema version mismatch" on
# the DEFAULT partition once ATTACH PARTITION has waited for it, where
# PostgreSQL loads every row and then fails the ATTACH. An unbatched COPY
# fails the same way.

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
  CREATE TABLE pt_def PARTITION OF pt DEFAULT;
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
step attach			{ ALTER TABLE pt ATTACH PARTITION pt3 FOR VALUES FROM (2000) TO (3000); }
step create_part	{ CREATE TABLE pt4 PARTITION OF pt FOR VALUES FROM (2000) TO (3000); }
step where			{ SELECT tableoid::regclass::text AS part, count(*) FROM pt GROUP BY 1 ORDER BY 1; }

session s3
step s3_lock		{ SELECT pg_advisory_lock(1); }
step s3_unlock		{ SELECT pg_advisory_unlock(1); }

permutation s3_lock s1_copy_def_first attach s3_unlock where
permutation s3_lock s1_copy_def_later attach s3_unlock where
permutation s3_lock s1_copy_def_later create_part s3_unlock where
