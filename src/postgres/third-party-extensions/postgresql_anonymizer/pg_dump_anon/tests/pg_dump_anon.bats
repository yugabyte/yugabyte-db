
initial_setup() {
  # destination folder
  mkdir -p $RESULTS
  # load database
  dropdb --if-exists $PGDATABASE
  psql -c "CREATE EXTENSION IF NOT EXISTS anon CASCADE" postgres
  psql -c "CREATE EXTENSION IF NOT EXISTS anon CASCADE" template1
  createdb $PGDATABASE
  psql -f $DIR/sql/roles.sql
  psql -f $DIR/sql/install.sql
}

setup() {
  load 'bats-support/load'
  load 'bats-assert/load'
  # make executable visible to PATH
  DIR=$( dirname "$BATS_TEST_FILENAME" )
  PATH="$PATH:../_build/linux/amd64/pg_dump_anon/"
  # Folders
  EXPECTED=$DIR/expected
  RESULTS=$DIR/_results
  # diff should ignore blank lines and comments
  DIFF='diff --ignore-matching-lines=^-- --ignore-blank-lines'
  PG_DUMP_ANON=../_build//linux/amd64/pg_dump_anon/pg_dump_anon
  # connection
  export PGHOST=127.0.0.1
  export PGUSER=postgres
  export PGPASSWORD=CHANGEME
  export PGPORT=5432
  export PGDATABASE=pg_dump_anon_regression
  # init the database if needed
  if [ "${BATS_TEST_NUMBER}" = 1 ];then
    initial_setup
  fi
}

final_teardown() {
  export PGUSER=postgres
  dropdb $PGDATABASE
  dropuser --if-exists maddy
}

teardown() {
  if [ "${#BATS_TEST_NAMES[@]}" -eq "$BATS_TEST_NUMBER" ]; then
    final_teardown
  fi
}

@test "Display the help page" {
  run $PG_DUMP_ANON --help
}

@test "Error when args are misplaced" {
  run $PG_DUMP_ANON pg_dump_anon_regression --port=5432
  assert_failure
}

@test "PGPASSWORD host md5 auth fail" {
  export PGPASSWORD=wrong_password
  run $PG_DUMP_ANON -h 127.0.0.1 -U maddy template1 > /dev/null
  assert_failure
}

@test "PGPASSWORD host md5 auth success" {
  export PGDATABASE=template1
  PGPASSWORD=CHANGEME $PG_DUMP_ANON --host=127.0.0.1 --username=maddy > /dev/null
}

@test "PGPASSWORD local md5 auth success" {
  run $PG_DUMP_ANON --host=/var/run/postgresql postgres
}

@test "PGPASS auth success" {
  unset PGPASSWORD
  echo "*:*:*:*:CHANGEME" > ~/.pgpass
  chmod 600 ~/.pgpass
  $PG_DUMP_ANON -h 127.0.0.1 -U maddy postgres > /dev/null
  rm ~/.pgpass
}

@test "PGPASSFILE auth success" {
  unset PGPASSWORD
  export PGPASSFILE=$(mktemp)
  echo "*:*:*:*:CHANGEME" > $PGPASSFILE
  chmod 600 $PGPASSFILE
  $PG_DUMP_ANON -h 127.0.0.1 -U maddy postgres > /dev/null
  rm $PGPASSFILE
}

@test "Export the entire db" {
  export PGUSER=maddy
  export PGPASSWORD=CHANGEME
  $PG_DUMP_ANON > $RESULTS/db.sql
  $DIFF $RESULTS/db.sql $EXPECTED
}

@test "Export a table" {
  export PGUSER=maddy
  export PGPASSWORD=CHANGEME
  $PG_DUMP_ANON -t owner > $RESULTS/owner.sql
  grep --invert-match --quiet 'CREATE TABLE public.company' $RESULTS/owner.sql
}

@test "Export 33% of a table" {
  psql -c "SECURITY LABEL FOR anon ON TABLE http_logs IS 'TABLESAMPLE BERNOULLI(33)';"
  $PG_DUMP_ANON -t http_logs > $RESULTS/https_logs_sample.sql
  lines=$(grep 'http'  https_logs_sample.sql|wc -l)
  psql -c "SECURITY LABEL FOR anon ON TABLE http_logs IS NULL;"
  [[ "$lines" < "100" ]]
}

@test "Export a sequence with uppercase letters" {
  $PG_DUMP_ANON | grep BuG_298
}

@test "Export to a file" {
  export PGUSER=maddy
  export PGPASSWORD=CHANGEME
  $PG_DUMP_ANON -t owner --file=$RESULTS/owner_2.sql pg_dump_anon_regression
  $DIFF $RESULTS/owner.sql $RESULTS/owner_2.sql
}



