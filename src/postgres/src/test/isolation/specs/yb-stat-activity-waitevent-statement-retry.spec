setup
{
  DROP TABLE IF EXISTS foo;
  CREATE TABLE foo (
    k	int		PRIMARY KEY,
    v	int 	NOT NULL
  ) SPLIT INTO 1 TABLETS;

  INSERT INTO foo SELECT generate_series(1, 100), 0;
}

teardown
{
  DROP TABLE foo;
}

session "s1"
setup {
  SET yb_transaction_priority_lower_bound = 0.6;
}

step "s1a" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1b" { SELECT * FROM foo WHERE k = 1 FOR UPDATE; }
step "s1c" {
  DO $$DECLARE r record;
  BEGIN
      WHILE true LOOP
          BEGIN
              SELECT wait_event_type, wait_event, state INTO STRICT r FROM pg_stat_activity WHERE query = 'update foo set v=10 where k=1;';

              IF r.wait_event_type = 'Timeout' THEN
                  IF r.wait_event = 'YBTxnConflictBackoff' THEN
                      -- NOTE: this message won't be visible in the test output until we get commit `ebd4992821` from upstream
                      RAISE NOTICE 'found wait event % %', r.wait_event_type, r.wait_event;
                      RETURN;
                  END IF;
              END IF;

              IF r.wait_event_type = 'Client' THEN
                  IF r.wait_event = 'ClientRead' THEN
                      RAISE EXCEPTION 's2 appears to have timed out: % state: %', r.wait_event, r.state;
                  END IF;
              END IF;


              -- We found the query, but it is not yet waiting due to a transaction conflict
              SELECT i into r from pg_stat_clear_snapshot() i;

          EXCEPTION
              WHEN NO_DATA_FOUND THEN
                  RAISE EXCEPTION 'expected update statement to be running';
          END;
      END LOOP;
  END$$;
}
step "s1d"	{ COMMIT; }

session "s2"
setup {
  SET yb_transaction_priority_upper_bound = 0.4;
}

step "s2a" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2b" { update foo set v=10 where k=1; }
step "s2c" { COMMIT; }

# FLAGS_enable_wait_queues must be false for this test, or the query will block in the wait queue
# instead of returning TransactionErrorCode::kConflict.
permutation "s1a" "s1b" "s2a" "s2b" "s1c" "s1d" "s2c"
