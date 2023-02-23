
set -ex

PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show search_path' | grep tpath
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show statement_timeout' | grep "5min"
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show lock_timeout' | grep "10s"

PGOPTIONS="-c              search_path=tpath -c statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show search_path' | grep tpath
PGOPTIONS="-c search_path=tpath -c              statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show statement_timeout' | grep "5min"
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c        lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show lock_timeout' | grep "10s"
PGOPTIONS="    -c     search_path=tpath    -c    statement_timeout=5min  -c        lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show lock_timeout' | grep "10s"

PGOPTIONS="--search_path=tpath --statement_timeout=5min --lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show search_path' | grep tpath
PGOPTIONS="--search_path=tpath --statement_timeout=5min --lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show statement_timeout' | grep "5min"
PGOPTIONS="--search_path=tpath --statement_timeout=5min --lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show lock_timeout' | grep "10s"

PGOPTIONS="     --search_path=tpath t=5min  10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show search_path' | grep tpath
PGOPTIONS=" --search_path=tpath --lock_timeout=10s              " psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show lock_timeout' | grep "10s"
PGOPTIONS="--search_path=tpath   --statement_timeout=5min      --lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show lock_timeout' | grep "10s"

PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock _timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show search_path' | grep tpath
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock _timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show statement_timeout' | grep "5min"
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock _timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show lock_timeout' | grep -v "10s"

PGOPTIONS="     -c search_path=tpath -c statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show search_path' | grep tpath
PGOPTIONS="-c search_path=tpath     -c statement_timeout=5min       -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show statement_timeout' | grep "5min"
PGOPTIONS="               -c search_path=tpath -c statement_timeout=5min      -c lock_timeout=10s    " psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show lock_timeout' | grep "10s"

PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout= " psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show search_path' | grep tpath
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout=" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show statement_timeout' | grep "5min"
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show statement_timeout' | grep "5min"
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show statement_timeout' | grep "5min"
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c      " psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'show statement_timeout' | grep "5min"

PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout=invalid" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'select 1'
PGOPTIONS="-c search_path=tpath -c statement_timeout=invalid -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'select 1'
PGOPTIONS="-c search_path=invalid -c statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=postgres" -c 'select 1'
