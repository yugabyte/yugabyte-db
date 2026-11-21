# A DDL that conflicts with a batched COPY's locks (ROWS_PER_TRANSACTION > 0)
# must wait for the whole COPY, as it does for an unbatched COPY and in
# PostgreSQL: the COPY carries its locks across each batch commit (GH #34963).
#
# Each permutation:
# - s3 holds advisory lock 1. copy_gate(), a CHECK constraint on each table,
#   waits for that lock at row 25, so a COPY of 40 rows in batches of 10 stops
#   in its third batch, after two batch commits.
# - s2 runs a DDL there. The (s1_copy_*) marker on the DDL step prints it as
#   waiting until COPY completes whether or not it blocked, so the s3 catalog
#   read that follows is the check: the DDL must not have taken effect yet.
# - s3 releases the lock and COPY finishes. A DDL that validates existing rows
#   (ADD CHECK, SET NOT NULL, ADD FOREIGN KEY) then fails on rows 21-40.
#
# The parent table of a foreign key is the exception. COPY runs its foreign
# key checks at the end of each batch and holds no lock on the parent until
# then, so a DDL on the parent does not wait, batched or not, as in
# PostgreSQL.

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
  CREATE TABLE t2 (k int PRIMARY KEY, b int, CONSTRAINT ck_a CHECK (b < 2000000000),
                   CONSTRAINT ck_b CHECK (b < 1000), CONSTRAINT a_gate CHECK (copy_gate(k)));
  CREATE TABLE pf (k int PRIMARY KEY, w int, CONSTRAINT gate CHECK (copy_gate(k))) PARTITION BY RANGE (k);
  CREATE TABLE pf1 PARTITION OF pf FOR VALUES FROM (1) TO (1000);
  CREATE TABLE pf2 PARTITION OF pf FOR VALUES FROM (1000) TO (2000);
  CREATE TABLE c (k int PRIMARY KEY, b int REFERENCES p (id), CONSTRAINT gate CHECK (copy_gate(k)));
  CREATE SEQUENCE sq;
  CREATE TABLE ts (k int PRIMARY KEY, b int, s int DEFAULT nextval('sq'), CONSTRAINT gate CHECK (copy_gate(k)));
}

teardown
{
  DROP TABLE t;
  DROP TABLE pt;
  DROP TABLE IF EXISTS pt1;
  DROP TABLE t2;
  DROP TABLE pf;
  DROP TABLE ts;
  DROP TABLE c;
  DROP SEQUENCE sq;
  DROP TABLE p;
  DROP FUNCTION reject_big_b();
  DROP FUNCTION copy_gate(int);
}

session s1
step s1_copy_t			{ COPY t FROM PROGRAM $$seq -f "%g,1" 1 20; seq -f "%g,999" 21 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_t_null		{ COPY t FROM PROGRAM $$seq -f "%g,1" 1 20; seq -f "%g," 21 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_t2		{ COPY t2 FROM PROGRAM $$seq -f "%g,1" 1 25; seq -f "%g,1500" 26 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_pf			{ COPY pf FROM PROGRAM $$seq -f "%g,1" 1 25; echo 1500,1; seq -f "%g,999" 26 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_pt			{ COPY pt FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_ts			{ COPY ts (k, b) FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_c			{ COPY c FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 10); }
step s1_copy_c_unbatched	{ COPY c FROM PROGRAM $$seq -f "%g,1" 1 40$$ WITH (FORMAT csv, ROWS_PER_TRANSACTION 0); }

session s2
step s2_create_trigger	{ CREATE TRIGGER reject_big_b BEFORE INSERT ON t FOR EACH ROW EXECUTE FUNCTION reject_big_b(); }
step s2_add_check		{ ALTER TABLE t ADD CONSTRAINT b_small CHECK (b < 100); }
step s2_set_not_null	{ ALTER TABLE t ALTER COLUMN b SET NOT NULL; }
step s2_add_fk			{ ALTER TABLE t ADD CONSTRAINT b_fk FOREIGN KEY (b) REFERENCES p (id); }
step s2_create_index	{ CREATE INDEX NONCONCURRENTLY t_b_idx ON t (b); }
step s2_index_partition	{ CREATE INDEX NONCONCURRENTLY pt1_b_idx ON pt1 (b); }
step s2_add_fk_pf		{ ALTER TABLE pf ADD CONSTRAINT pf_fk FOREIGN KEY (w) REFERENCES p (id); }
step s2_add_column		{ ALTER TABLE t ADD COLUMN x int DEFAULT 7; }
step s2_drop_check		{ ALTER TABLE t2 DROP CONSTRAINT ck_a; }
step s2_detach			{ ALTER TABLE pt DETACH PARTITION pt1; }
step s2_alter_sequence	{ ALTER SEQUENCE sq MAXVALUE 1000000; }
step s2_alter_fk_parent	{ ALTER TABLE p ADD COLUMN note text; }
step s2_count_c			{ SELECT count(*) FROM c; }
step s2_counts_t		{ SELECT b, count(*) FROM t GROUP BY b ORDER BY b; }
step s2_count_pt		{ SELECT count(*) FROM pt; }
step s2_pf_result		{ SELECT count(*) AS loaded, count(*) FILTER (WHERE w NOT IN (SELECT id FROM p)) AS orphans, (SELECT count(*) FROM pg_constraint WHERE conrelid = 'pf'::regclass AND contype = 'f' AND convalidated) AS valid_fks FROM pf; }
step s2_count_t2		{ SELECT count(*), max(b) FROM t2; }
step s2_where_pt1		{ SELECT (SELECT count(*) FROM pt) AS via_parent, (SELECT count(*) FROM pt1) AS in_pt1; }
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
step s3_parent_columns	{ SELECT count(*) FROM pg_attribute WHERE attrelid = 'p'::regclass AND attname = 'note'; }
step s3_columns		{ SELECT count(*) FROM pg_attribute WHERE attrelid = 't'::regclass AND attname = 'x'; }
step s3_pf_fk			{ SELECT count(*) FROM pg_constraint WHERE conrelid = 'pf'::regclass AND contype = 'f'; }
step s3_ck_a			{ SELECT count(*) FROM pg_constraint WHERE conrelid = 't2'::regclass AND conname = 'ck_a'; }
step s3_pt1_attached	{ SELECT count(*) FROM pg_inherits WHERE inhrelid = 'pt1'::regclass; }
step s3_sequence_max	{ SELECT seqmax FROM pg_sequence WHERE seqrelid = 'sq'::regclass; }

# The target table.
permutation s3_lock s1_copy_t s2_create_trigger(s1_copy_t) s3_triggers s3_unlock s2_triggers s2_counts_t
permutation s3_lock s1_copy_t s2_add_check(s1_copy_t) s3_constraints s3_unlock s2_constraints s2_counts_t
permutation s3_lock s1_copy_t_null s2_set_not_null(s1_copy_t_null) s3_not_null s3_unlock s2_not_null s2_counts_t
permutation s3_lock s1_copy_t s2_add_fk(s1_copy_t) s3_constraints s3_unlock s2_constraints s2_counts_t
permutation s3_lock s1_copy_t s2_create_index(s1_copy_t) s3_indexes s3_unlock s2_index_check s2_counts_t
permutation s3_lock s1_copy_t s2_add_column(s1_copy_t) s3_columns s3_unlock s2_counts_t

# Dropping one CHECK constraint must not stop COPY from checking the other:
# rows 26-40 violate ck_b, so COPY fails at row 26. a_gate sorts before ck_a,
# so a COPY that kept the constraint expressions of its first batch would
# check ck_b with the expression of ck_a and let those rows in.
permutation s3_lock s1_copy_t2 s2_drop_check(s1_copy_t2) s3_ck_a s3_unlock s2_count_t2

# A foreign key added to a partitioned target: rows 26-40 reference no row
# of p, so ADD FOREIGN KEY must fail once COPY has loaded them. Row 1500 goes
# to a partition that COPY opens only after the DDL.
permutation s3_lock s1_copy_pf s2_add_fk_pf(s1_copy_pf) s3_pf_fk s3_unlock s2_pf_result

# A partition of the target table.
permutation s3_lock s1_copy_pt s2_index_partition(s1_copy_pt) s3_indexes s3_unlock s2_count_pt
permutation s3_lock s1_copy_pt s2_detach(s1_copy_pt) s3_pt1_attached s3_unlock s2_where_pt1

# A sequence that a column default of the target table reads.
permutation s3_lock s1_copy_ts s2_alter_sequence(s1_copy_ts) s3_sequence_max s3_unlock s2_count_ts

# The parent table of a foreign key on the target table: the DDL does not wait.
permutation s3_lock s1_copy_c s2_alter_fk_parent s3_parent_columns s3_unlock s2_count_c
permutation s3_lock s1_copy_c_unbatched s2_alter_fk_parent s3_parent_columns s3_unlock s2_count_c
