#!/bin/bash -x

set -ex

/usr/bin/odyssey /tsa/tsa.conf

psql -h localhost -p 6432 -U user_ro -c "SELECT pg_is_in_recovery()" tsa_db | grep 't' > /dev/null 2>&1 || {
  echo "ERROR: failed auth with hba trust, correct password and plain password in config"

	cat /var/log/odyssey.log
	echo "

	"
	cat /var/log/postgresql/postgresql-14-repl.log

	exit 1
}

psql -h localhost -p 6432 -U user_rw -c "SELECT pg_is_in_recovery()" tsa_db | grep 'f' > /dev/null 2>&1 || {
  echo "ERROR: failed auth with hba trust, correct password and plain password in config"

	cat /var/log/odyssey.log
	echo "

	"
	cat /var/log/postgresql/postgresql-14-main.log

	exit 1
}


ody-stop
