
set -ex

PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=useropt" -c 'show search_path' | grep tpath
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=useropt" -c 'show statement_timeout' | grep "0" # override
PGOPTIONS="-c search_path=tpath -c statement_timeout=5min -c lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=useropt" -c 'show lock_timeout' | grep "10s"

PGOPTIONS="--search_path=tpath --statement_timeout=5min --lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=useropt" -c 'show search_path' | grep tpath
PGOPTIONS="--search_path=tpath --statement_timeout=5min --lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=useropt" -c 'show statement_timeout' | grep "0" #override
PGOPTIONS="--search_path=tpath --statement_timeout=5min --lock_timeout=10s" psql "host=localhost port=6432 dbname=postgres user=useropt" -c 'show lock_timeout' | grep "10s"

