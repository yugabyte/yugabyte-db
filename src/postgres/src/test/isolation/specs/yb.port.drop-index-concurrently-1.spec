# DROP INDEX CONCURRENTLY
#
# Verify that an index remains usable and maintained by transactions that
# observed it before a concurrent drop started.

setup
{
	CREATE TABLE test_dc(id serial primary key, data int);
	INSERT INTO test_dc(data) SELECT * FROM generate_series(1, 100);
	CREATE INDEX test_dc_data ON test_dc(data);
}

teardown
{
	DROP TABLE test_dc;
}

session s1
step prepi { PREPARE getrow_idxscan AS SELECT * FROM test_dc WHERE data = 34 ORDER BY id,data; }
step preps { PREPARE getrow_seqscan AS SELECT * FROM test_dc WHERE data = 34 ORDER BY id,data; }
step begin { BEGIN; }
step disableseq { SET enable_seqscan = false; }
step explaini { EXPLAIN (COSTS OFF) EXECUTE getrow_idxscan; }
step enableseq { SET enable_seqscan = true; }
step disableindex { SET enable_indexscan = false; }
step explains { EXPLAIN (COSTS OFF) EXECUTE getrow_seqscan; }
step selecti { EXECUTE getrow_idxscan; }
step selects { EXECUTE getrow_seqscan; }
step end { COMMIT; }

session s2
setup { BEGIN; }
step select2 { SELECT * FROM test_dc WHERE data = 34 ORDER BY id,data; }
step insert2 { INSERT INTO test_dc(data) SELECT * FROM generate_series(1, 100); }
step end2 { COMMIT; }

session s3
step drop { DROP INDEX CONCURRENTLY test_dc_data; }

permutation prepi preps begin disableseq explaini enableseq disableindex explains select2 drop insert2 end2 selecti selects end
