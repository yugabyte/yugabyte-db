#!/bin/bash -x

set -ex

/usr/bin/odyssey /auth_query/config.conf

PGPASSWORD=passwd psql -h localhost -p 6432 -U auth_query_user_scram_sha_256 -c "SELECT 1" auth_query_db >/dev/null 2>&1 || {
	echo "ERROR: failed backend auth with correct password"

	cat /var/log/odyssey.log
	echo "

	"
	cat /var/log/postgresql/postgresql-14-main.log

	exit 1
}

PGPASSWORD=passwd psql -h localhost -p 6432 -U auth_query_user_md5 -c "SELECT 1" auth_query_db >/dev/null 2>&1 || {
	echo "ERROR: failed backend auth with correct password"

	cat /var/log/odyssey.log
	echo "

	"
	cat /var/log/postgresql/postgresql-14-main.log

	exit 1
}


ody-stop
