setup
{
  CREATE TABLE IF NOT EXISTS products (
    product_id integer,
    date_added timestamp,
    name text,
    price numeric,
    discounted_price numeric,
    PRIMARY KEY(product_id hash, date_added)
  );

  CREATE TABLE IF NOT EXISTS fk_pkey_table
  (
      id integer PRIMARY KEY,
      product_id integer,
      date_added timestamp,
      FOREIGN KEY (product_id, date_added) REFERENCES products (product_id, date_added)
  );

  INSERT INTO products VALUES (1,'2022-01-01 05:00:00', 'oats', 10, 1);
}

teardown
{
  DROP TABLE fk_pkey_table, products;
}

session "s1"
step "s1_serializable_txn"    { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s1_repeatable_read_txn" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_read_committed_txn"  { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s1_fk_pkey_insert" {
  INSERT INTO fk_pkey_table VALUES(1, 1, '2022-01-01 05:00:00');
}
step "s1_commit"                   { COMMIT; }
step "s1_select_for_key_share"     { SELECT * FROM products FOR KEY SHARE; }
step "s1_select_for_share"         { SELECT * FROM products FOR SHARE; }
step "s1_select_for_no_key_update" { SELECT * FROM products FOR NO KEY UPDATE; }
step "s1_select_for_update"        { SELECT * FROM products FOR UPDATE; }
step "s1_select"                   { SELECT * FROM products; }
# Note that priorities will only take effect/make sense when this test is run in Fail-on-Conflict
# concurrency control mode and the isolation is not read committed.
step "s1_priority"                 { SET yb_transaction_priority_lower_bound = .9; }


session "s2"
step "s2_serializable_txn"    { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s2_repeatable_read_txn" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_read_committed_txn"  { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s2_update"              { UPDATE products SET price = 2 WHERE product_id = 1; }
step "s2_commit"              { COMMIT; }
step "s2_select"              { SELECT * from products; }
step "s2_priority"            { SET yb_transaction_priority_upper_bound= .1; }

# Concurrent updates with foreign key constraints
#
# An insert on a table with a foreign key constraint results in a FOR KEY SHARE lock on the foreign key. These
# tests validate that the FOR KEY SHARE lock does not result in blocked transactions.

permutation "s1_priority" "s2_priority" "s1_serializable_txn" "s1_fk_pkey_insert" "s2_update" "s1_commit" "s1_select"
permutation "s1_priority" "s2_priority" "s1_repeatable_read_txn" "s1_fk_pkey_insert" "s2_update" "s1_commit" "s1_select"
permutation "s1_priority" "s2_priority" "s1_read_committed_txn" "s1_fk_pkey_insert" "s2_update" "s1_commit" "s1_select"

permutation "s1_priority" "s2_priority" "s1_serializable_txn" "s2_serializable_txn" "s1_fk_pkey_insert" "s2_update" "s1_commit" "s2_commit" "s1_select"
permutation "s1_priority" "s2_priority" "s1_repeatable_read_txn" "s2_repeatable_read_txn" "s1_fk_pkey_insert" "s2_update" "s1_commit" "s2_commit" "s1_select"
permutation "s1_priority" "s2_priority" "s1_read_committed_txn" "s2_read_committed_txn" "s1_fk_pkey_insert" "s2_update" "s1_commit" "s2_commit" "s1_select"

# SELECT FOR <CONDITION> tests

# Unlike in Postgres these queries will block. Any read on a row in serializable will take a STRONG_READ lock
# on those rows.
permutation "s1_priority" "s2_priority" "s1_serializable_txn" "s2_serializable_txn" "s1_select_for_key_share" "s2_update" "s1_commit" "s2_commit" "s1_select"

# These queries should have identical behavior to Postgres
permutation "s1_priority" "s2_priority" "s1_read_committed_txn" "s2_read_committed_txn" "s1_select_for_key_share" "s2_update" "s1_commit" "s2_commit" "s1_select"
permutation "s1_priority" "s2_priority" "s1_repeatable_read_txn" "s2_repeatable_read_txn" "s1_select_for_key_share" "s2_update" "s1_commit" "s2_commit" "s1_select"

permutation "s1_priority" "s2_priority" "s1_read_committed_txn" "s2_read_committed_txn" "s1_select_for_share" "s2_update" "s1_commit" "s2_commit" "s1_select"
permutation "s1_priority" "s2_priority" "s1_repeatable_read_txn" "s2_repeatable_read_txn" "s1_select_for_share" "s2_update" "s1_commit" "s2_commit" "s1_select"

permutation "s1_priority" "s2_priority" "s1_read_committed_txn" "s2_read_committed_txn" "s1_select_for_no_key_update" "s2_update" "s1_commit" "s2_commit" "s1_select"
permutation "s1_priority" "s2_priority" "s1_repeatable_read_txn" "s2_repeatable_read_txn" "s1_select_for_no_key_update" "s2_update" "s1_commit" "s2_commit" "s1_select"

permutation "s1_priority" "s2_priority" "s1_read_committed_txn" "s2_read_committed_txn" "s1_select_for_update" "s2_update" "s1_commit" "s2_commit" "s1_select"
permutation "s1_priority" "s2_priority" "s1_repeatable_read_txn" "s2_repeatable_read_txn" "s1_select_for_update" "s2_update" "s1_commit" "s2_commit" "s1_select"
