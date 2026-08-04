#! /bin/bash

IMG=registry.gitlab.com/dalibo/postgresql_anonymizer
BOX="docker run --rm -i $IMG /anon.sh"

# Create a demo database
createdb blackbox_demo_db

createuser blackbox_demo_owner
createuser blackbox_demo_writer
createuser blackbox_demo_reader

SQL="
CREATE TABLE people(
  firstname TEXT,
  lastname TEXT
);

ALTER TABLE people OWNER TO blackbox_demo_owner;

INSERT INTO people VALUES ('Sarah', 'Conor');

GRANT SELECT ON TABLE public.people TO blackbox_demo_reader;
GRANT SELECT,INSERT,DELETE,UPDATE ON TABLE public.people TO blackbox_demo_writer;
"

psql blackbox_demo_db -c "$SQL"

# Write the masking rules
cat <<EOF >> _blackbox_rules.sql
SELECT pg_catalog.set_config('search_path', 'public', false);

CREATE EXTENSION anon CASCADE;
SELECT anon.load();

SECURITY LABEL FOR anon ON COLUMN people.lastname
IS 'MASKED WITH FUNCTION anon.fake_last_name()';
EOF

# For debug
pg_dump blackbox_demo_db > _blackbox_demo_dump.sql

# Pass the dump and the rules throught the docker "black box"
(pg_dumpall --roles-only && pg_dump blackbox_demo_db) | cat - _blackbox_rules.sql | $BOX > _blackbox_demo_dump_anonymized.sql

cat _blackbox_demo_dump_anonymized.sql | grep 'Sarah'

# drop the demo database
dropdb blackbox_demo_db
dropuser blackbox_demo_owner
dropuser blackbox_demo_reader
dropuser blackbox_demo_writer
