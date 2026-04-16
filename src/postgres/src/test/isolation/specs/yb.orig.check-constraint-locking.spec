setup
{
  CREATE TABLE IF NOT EXISTS products (
    product_id integer,
    date_added timestamp,
    name text,
    price numeric,
    discounted_price numeric,
    date_expires timestamptz,
    CHECK (price > 0),
    CHECK (discounted_price > 0),
    CHECK (date_expires > date_added),
    CHECK (price > discounted_price),
    PRIMARY KEY((product_id) HASH, date_added)
  );

  INSERT INTO products VALUES (1,'2022-01-01 05:00:00', 'oats', 10, 1, '2022-12-31 23:59:59');
}

teardown
{
  DROP TABLE products;
}

session "s1"
# Note that priorities will only take effect/make sense when this test is run in Fail-on-Conflict
# concurrency control mode and the isolation is not read committed.
setup {
  SET yb_transaction_priority_lower_bound = .9;
}

step "s1_serializable_txn"    { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s1_repeatable_read_txn" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_read_committed_txn"  { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s1_upd_price"           { UPDATE products SET price = 3 WHERE product_id = 1; }
step "s1_upd_pk_date_added"   { UPDATE products SET date_added = '2022-06-01 01:00:00' WHERE product_id = 1; }
step "s1_upd_date_expires"    { UPDATE products SET date_expires = '2022-05-01 01:00:00' WHERE product_id = 1; }
step "s1_commit" { COMMIT; }
step "s1_select" { SELECT * FROM products; }


session "s2"
setup {
  SET yb_transaction_priority_upper_bound= .1;
}

step "s2_serializable_txn"    { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s2_repeatable_read_txn" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_read_committed_txn"  { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s2_upd_price"           { UPDATE products SET price = 2 WHERE product_id = 1; }
step "s2_upd_disc_price"      { UPDATE products SET discounted_price = 5 WHERE product_id = 1; }
step "s2_upd_pk_product_id"   { UPDATE products SET product_id = 12 WHERE product_id = 1; }
step "s2_upd_pk_date_added"   { UPDATE products SET date_added = '2022-06-01 02:00:00' WHERE product_id = 1; }
step "s2_upd_date_expires"    { UPDATE products SET date_expires = '2022-05-01 02:00:00' WHERE product_id = 1; }
step "s2_insert_on_conflict1" {
    INSERT INTO products VALUES(1,'2022-01-01 05:00:00', 'fish', 11, 2, '2022-12-30 23:59:59')
    ON CONFLICT(product_id, date_added) DO UPDATE SET discounted_price = 5;
}
step "s2_insert_on_conflict2" {
    INSERT INTO products VALUES(1,'2022-01-01 05:00:00', 'fish', 11, 2, '2022-12-30 23:59:59')
    ON CONFLICT(product_id, date_added) DO UPDATE SET date_expires = '2022-05-01 02:00:00';
}
step "s2_insert_on_conflict3" {
    INSERT INTO products VALUES(1,'2022-01-01 05:00:00', 'fish', 11, 2, '2022-12-30 23:59:59')
    ON CONFLICT(product_id, date_added) DO UPDATE SET date_added = '2022-05-01 02:00:00';
}
step "s2_commit" { COMMIT; }
step "s2_select" { SELECT * from products; }
# In Wait-On-Conflict tests we set the retry limit to 0 in order to match Postgres semantics.
# However, retries must always be enabled in READ COMMITTED in order to match READ COMMITTED
# semantics
step "s2_enable_retries" { SET yb_max_query_layer_retries = 60; }

# Concurrent updates with check constraints

# ===========================
#        SERIALIZABLE
# ===========================

permutation "s1_select" "s1_serializable_txn" "s2_serializable_txn" "s1_upd_price" "s2_upd_disc_price" "s1_commit" "s2_commit" "s1_select"
permutation "s1_select" "s1_serializable_txn" "s2_serializable_txn" "s1_upd_pk_date_added" "s2_upd_date_expires" "s1_commit" "s2_commit" "s1_select"
permutation "s1_select" "s1_serializable_txn" "s2_serializable_txn" "s1_upd_price" "s2_insert_on_conflict1" "s1_commit" "s2_commit" "s1_select"
permutation "s1_select" "s1_serializable_txn" "s2_serializable_txn" "s1_upd_pk_date_added" "s2_insert_on_conflict2" "s1_commit" "s2_commit" "s1_select"
permutation "s1_select" "s1_serializable_txn" "s2_serializable_txn" "s1_upd_date_expires" "s2_insert_on_conflict3" "s1_commit" "s2_commit" "s1_select"

# ===========================
#      REPEATABLE READ
# ===========================

# Because we have automatic retry on the first statement of a transaction,
# UPDATE with no SELECT should either succeed, or detect constraint violations
permutation "s1_repeatable_read_txn" "s1_select" "s2_repeatable_read_txn" "s1_upd_price" "s2_upd_disc_price" "s1_commit" "s2_commit" "s1_select"
permutation "s1_repeatable_read_txn" "s1_select" "s2_repeatable_read_txn" "s1_upd_pk_date_added" "s2_upd_date_expires" "s1_commit" "s2_commit" "s1_select"
permutation "s1_repeatable_read_txn" "s1_select" "s2_repeatable_read_txn" "s1_upd_price" "s2_insert_on_conflict1" "s1_commit" "s2_commit" "s1_select"
# TODO: enable this test after #17579 has been resolved
# permutation "s1_repeatable_read_txn" "s1_select" "s2_repeatable_read_txn" "s1_upd_pk_date_added" "s2_insert_on_conflict2" "s1_commit" "s2_commit" "s1_select"
permutation "s1_repeatable_read_txn" "s1_select" "s2_repeatable_read_txn" "s1_upd_date_expires" "s2_insert_on_conflict3" "s1_commit" "s2_commit" "s1_select"

# ===========================
#       READ COMMITTED
# ===========================

permutation "s2_enable_retries" "s1_read_committed_txn" "s1_select" "s2_read_committed_txn" "s1_upd_price" "s2_upd_disc_price" "s1_commit" "s2_commit" "s1_select"
permutation "s2_enable_retries" "s1_read_committed_txn" "s1_select" "s2_read_committed_txn" "s1_upd_pk_date_added" "s2_upd_date_expires" "s1_commit" "s2_commit" "s1_select"
permutation "s2_enable_retries" "s1_read_committed_txn" "s1_select" "s2_read_committed_txn" "s1_upd_price" "s2_insert_on_conflict1" "s1_commit" "s2_commit" "s1_select"
permutation "s2_enable_retries" "s1_read_committed_txn" "s1_select" "s2_read_committed_txn" "s1_upd_pk_date_added" "s2_insert_on_conflict2" "s1_commit" "s2_commit" "s1_select"
permutation "s2_enable_retries" "s1_read_committed_txn" "s1_select" "s2_read_committed_txn" "s1_upd_date_expires" "s2_insert_on_conflict3" "s1_commit" "s2_commit" "s1_select"
