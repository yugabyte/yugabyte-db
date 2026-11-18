#!/bin/sh

export PGPORT=${PGPORT:-28817}
export PGHOST=127.0.0.1

dropdb my_db

set -e

createdb my_db

psql my_db <<EOF
create table people (firstname varchar, lastname varchar);
insert into people (firstname, lastname) values ('daffy', 'duck');
insert into people (firstname, lastname) values ('speedy', 'gonzales');
insert into people (firstname, lastname) values ('fat', 'freddy');
EOF

pg_dump --format=plain my_db > dump.sql

cat > rules.sql <<EOF
RESET search_path;
SECURITY LABEL FOR anon ON COLUMN people.lastname
  IS 'MASKED WITH FUNCTION anon.dummy_last_name()';
EOF

IMG=registry.gitlab.com/dalibo/postgresql_anonymizer
ANON="docker run --rm -i $IMG /dump.sh"
cat dump.sql rules.sql | $ANON
