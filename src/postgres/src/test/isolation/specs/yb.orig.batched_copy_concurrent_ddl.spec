# A DDL must wait for a batched COPY (ROWS_PER_TRANSACTION > 0) to finish, as
# it waits for an unbatched one. COPY commits a YB transaction per batch, and
# the object locks it took in earlier batches must survive those commits
# (GH #34963).
#
# Every COPY loads 40 rows in batches of 10. A CHECK constraint calls
# copy_gate(), which takes advisory lock 1 at row 25, so COPY stops in its
# third batch, after two batch commits, until s3 releases the lock. s2 runs
# its DDL while COPY is stopped there, and s3 then reads the catalog to show
# that the DDL has not taken effect. The DDL must block until COPY completes;
# a DDL that validates existing rows then fails against rows 21-40.
#
# The targets cover each kind of lock COPY holds: the table and its indexes,
# a partition (locked when the first row is routed to it), and a sequence used
# by a column default.

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
  CREATE FUNCTION reject_big_b() RETURNS trigger LANGUAGE plpgsql AS
    $$ BEGIN IF NEW.b > 100 THEN RAISE EXCEPTION 'rejected b=%', NEW.b; END IF; RETURN NEW; END $$;
  CREATE TABLE p (id int PRIMARY KEY);
  INSERT INTO p VALUES (1);
  CREATE TABLE t (k int PRIMARY KEY, b int, CONSTRAINT gate CHECK (copy_gate(k)));
  CREATE TABLE pt (k int PRIMARY KEY, b int, CONSTRAINT gate CHECK (copy_gate(k))) PARTITION BY RANGE (k);
  CREATE TABLE pt1 PARTITION OF pt FOR VALUES FROM (1) TO (1000);
  CREATE SEQUENCE sq;
  CREATE TABLE ts (k int PRIMARY KEY, b int, s int DEFAULT nextval('sq'), CONSTRAINT gate CHECK (copy_gate(k)));
}

teardown
{
  DROP TABLE t;
  DROP TABLE pt;
  DROP TABLE ts;
  DROP SEQUENCE sq;
  DROP TABLE p;
  DROP FUNCTION reject_big_b();
  DROP FUNCTION copy_gate(int);
}

session s1
step s1_copy_t			{ COPY t FROM PROGRAM $$seq -f "%g,1" 1 20; seq -f "%g,999" 21 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_t_null		{ COPY t FROM PROGRAM $$seq -f "%g,1" 1 20; seq -f "%g," 21 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_pt			{ COPY pt FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_ts			{ COPY ts (k, b) FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }

session s2
step s2_create_trigger	{ CREATE TRIGGER reject_big_b BEFORE INSERT ON t FOR EACH ROW EXECUTE FUNCTION reject_big_b(); }
step s2_add_check		{ ALTER TABLE t ADD CONSTRAINT b_small CHECK (b < 100); }
step s2_set_not_null	{ ALTER TABLE t ALTER COLUMN b SET NOT NULL; }
step s2_add_fk			{ ALTER TABLE t ADD CONSTRAINT b_fk FOREIGN KEY (b) REFERENCES p (id); }
step s2_create_index	{ CREATE INDEX NONCONCURRENTLY t_b_idx ON t (b); }
step s2_index_partition	{ CREATE INDEX NONCONCURRENTLY pt1_b_idx ON pt1 (b); }
step s2_alter_sequence	{ ALTER SEQUENCE sq MAXVALUE 1000000; }
step s2_counts_t		{ SELECT b, count(*) FROM t GROUP BY b ORDER BY b; }
step s2_count_pt		{ SELECT count(*) FROM pt; }
step s2_count_ts		{ SELECT count(*) FROM ts; }
step s2_constraints		{ SELECT conname, convalidated FROM pg_constraint WHERE conrelid = 't'::regclass AND contype IN ('c', 'f') ORDER BY conname; }
step s2_not_null		{ SELECT attnotnull FROM pg_attribute WHERE attrelid = 't'::regclass AND attname = 'b'; }
step s2_triggers		{ SELECT tgname FROM pg_trigger WHERE tgrelid = 't'::regclass AND NOT tgisinternal; }
step s2_index_check		{ SELECT yb_index_check('t_b_idx'::regclass); }

session s3
step s3_lock			{ SELECT pg_advisory_lock(1); }
step s3_unlock			{ SELECT pg_advisory_unlock(1); }
step s3_triggers		{ SELECT count(*) FROM pg_trigger WHERE tgrelid = 't'::regclass AND NOT tgisinternal; }
step s3_constraints		{ SELECT count(*) FROM pg_constraint WHERE conrelid = 't'::regclass AND conname IN ('b_small', 'b_fk'); }
step s3_not_null		{ SELECT attnotnull FROM pg_attribute WHERE attrelid = 't'::regclass AND attname = 'b'; }
step s3_indexes			{ SELECT count(*) FROM pg_class WHERE relname IN ('t_b_idx', 'pt1_b_idx'); }
step s3_sequence_max	{ SELECT seqmax FROM pg_sequence WHERE seqrelid = 'sq'::regclass; }

# The target table.
permutation s3_lock s1_copy_t s2_create_trigger(s1_copy_t) s3_triggers s3_unlock s2_triggers s2_counts_t
permutation s3_lock s1_copy_t s2_add_check(s1_copy_t) s3_constraints s3_unlock s2_constraints s2_counts_t
permutation s3_lock s1_copy_t_null s2_set_not_null(s1_copy_t_null) s3_not_null s3_unlock s2_not_null s2_counts_t
permutation s3_lock s1_copy_t s2_add_fk(s1_copy_t) s3_constraints s3_unlock s2_constraints s2_counts_t
permutation s3_lock s1_copy_t s2_create_index(s1_copy_t) s3_indexes s3_unlock s2_index_check s2_counts_t

# A partition of the target table.
permutation s3_lock s1_copy_pt s2_index_partition(s1_copy_pt) s3_indexes s3_unlock s2_count_pt

# A sequence that a column default of the target table reads.
permutation s3_lock s1_copy_ts s2_alter_sequence(s1_copy_ts) s3_sequence_max s3_unlock s2_count_ts
