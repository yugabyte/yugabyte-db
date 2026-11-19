# A batched COPY (ROWS_PER_TRANSACTION > 0) loads each batch as a COPY of its
# own, in its own transaction. A DDL on an object that the batch in progress
# has locked waits for that batch, not for the whole COPY, and the next batch
# sees what the DDL did: it takes its locks and builds its indexes, partition
# routing, constraints and triggers from the catalog again (GH #34963).
#
# Every COPY loads 40 rows in batches of 10. A CHECK constraint calls
# copy_gate(), which takes advisory lock 1 at row 25, so COPY stops in its
# third batch until s3 releases the lock. s2 runs its DDL while COPY is
# stopped there. The expected outcome of each permutation is what PostgreSQL
# 15 does when the same rows are loaded by four COPY commands and the DDL is
# issued during the third.

setup
{
  DO $$ BEGIN IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'copier') THEN CREATE ROLE copier; END IF; END $$;
  GRANT pg_execute_server_program TO copier;
  CREATE FUNCTION copy_gate(k int) RETURNS bool LANGUAGE plpgsql AS $$
    BEGIN
      IF k = 25 THEN
        PERFORM pg_advisory_lock(1);
        PERFORM pg_advisory_unlock(1);
      END IF;
      RETURN true;
    END $$;
  CREATE FUNCTION reject_big_b() RETURNS trigger LANGUAGE plpgsql AS
    $$ BEGIN IF NEW.b > 100 THEN RAISE EXCEPTION 'rejected b=%', NEW.b; END IF; RETURN NEW; END $$;
  CREATE TABLE p (id int PRIMARY KEY);
  INSERT INTO p VALUES (1);
  CREATE TABLE t (k int PRIMARY KEY, b int, CONSTRAINT gate CHECK (copy_gate(k)));
  ALTER TABLE t ADD CONSTRAINT k_pos CHECK (k > 0) NOT VALID;
  GRANT INSERT ON t TO copier;
  CREATE TABLE pt (k int PRIMARY KEY, b int, CONSTRAINT gate CHECK (copy_gate(k))) PARTITION BY RANGE (k);
  CREATE TABLE pt1 PARTITION OF pt FOR VALUES FROM (1) TO (21);
  CREATE TABLE pt2 PARTITION OF pt FOR VALUES FROM (21) TO (1000);
  CREATE TABLE pt3 (k int PRIMARY KEY, b int, CONSTRAINT gate CHECK (copy_gate(k)));
  CREATE TABLE c (k int PRIMARY KEY, b int REFERENCES p (id), CONSTRAINT gate CHECK (copy_gate(k)));
  CREATE SEQUENCE sq;
  CREATE TABLE ts (k int PRIMARY KEY, b int, s int DEFAULT nextval('sq'), CONSTRAINT gate CHECK (copy_gate(k)));
}

teardown
{
  DROP TABLE IF EXISTS t; DROP TABLE IF EXISTS t_renamed; DROP TABLE pt; DROP TABLE IF EXISTS pt2; DROP TABLE IF EXISTS pt3; DROP TABLE c; DROP TABLE ts; DROP SEQUENCE sq; DROP TABLE p;
  DROP FUNCTION reject_big_b(); DROP FUNCTION copy_gate(int);
}

session s1
step s1_copy_t		{ COPY t FROM PROGRAM $$seq -f "%g,1" 1 20; seq -f "%g,999" 21 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_set_role	{ SET ROLE copier; }
step s1_reset_role	{ RESET ROLE; }
step s1_copy_t1		{ COPY t FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_pt		{ COPY pt FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_c		{ COPY c FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_ts		{ COPY ts (k, b) FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }

session s2
step create_trigger	{ CREATE TRIGGER reject_big_b BEFORE INSERT ON t FOR EACH ROW EXECUTE FUNCTION reject_big_b(); }
step create_index_nc	{ CREATE INDEX NONCONCURRENTLY t_b_idx ON t (b); }
step create_index	{ CREATE INDEX t_b_idx ON t (b); }
step index_check	{ SELECT yb_index_check('t_b_idx'::regclass); }
step add_column		{ ALTER TABLE t ADD COLUMN x int; }
step validate		{ ALTER TABLE t VALIDATE CONSTRAINT k_pos; }
step analyze		{ ANALYZE t; }
step revoke			{ REVOKE INSERT ON t FROM copier; }
step grant			{ GRANT SELECT ON t TO copier; }
step truncate		{ TRUNCATE t; }
step index_pt2		{ CREATE INDEX NONCONCURRENTLY pt2_b_idx ON pt2 (b); }
step attach			{ ALTER TABLE pt ATTACH PARTITION pt3 FOR VALUES FROM (1000) TO (2000); }
step detach			{ ALTER TABLE pt DETACH PARTITION pt2; }
step alter_fk_parent	{ ALTER TABLE p ADD COLUMN note text; }
step alter_sequence	{ ALTER SEQUENCE sq MAXVALUE 1000000; }
step count_t		{ SELECT count(*) FROM t; }
step count_pt		{ SELECT tableoid::regclass::text AS part, count(*) FROM pt GROUP BY 1 ORDER BY 1; }
step count_c		{ SELECT count(*) FROM c; }
step count_ts		{ SELECT count(*) FROM ts; }

session s3
step s3_lock		{ SELECT pg_advisory_lock(1); }
step s3_unlock		{ SELECT pg_advisory_unlock(1); }

# The DDL waits for the batch; the last batch runs with the trigger and fails.
permutation s3_lock s1_copy_t create_trigger s3_unlock count_t

# The DDL waits for the batch; the last batch maintains the new index.
permutation s3_lock s1_copy_t create_index_nc s3_unlock count_t
permutation s3_lock s1_copy_t create_index s3_unlock index_check count_t

# The DDL waits for the batch; the input no longer fits the table.
permutation s3_lock s1_copy_t add_column s3_unlock count_t

# The DDL waits for the batch and removes the rows loaded so far.
permutation s3_lock s1_copy_t truncate s3_unlock count_t

# These do not conflict with the lock of the batch.
permutation s3_lock s1_copy_t validate s3_unlock count_t
permutation s3_lock s1_copy_t analyze s3_unlock count_t

# Each batch checks the privilege again.
permutation s3_lock s1_set_role s1_copy_t1 grant s3_unlock s1_reset_role count_t
permutation s3_lock s1_set_role s1_copy_t1 revoke s3_unlock s1_reset_role count_t

# Partitions: an index on the partition being loaded, a partition attached,
# and the partition being loaded detached.
permutation s3_lock s1_copy_pt index_pt2 s3_unlock count_pt
permutation s3_lock s1_copy_pt attach s3_unlock count_pt
permutation s3_lock s1_copy_pt detach s3_unlock count_pt

# The parent table of a foreign key, and a sequence that a column default
# reads.
permutation s3_lock s1_copy_c alter_fk_parent s3_unlock count_c
permutation s3_lock s1_copy_ts alter_sequence s3_unlock count_ts
