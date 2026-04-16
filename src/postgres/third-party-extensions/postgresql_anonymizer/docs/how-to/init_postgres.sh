#!/bin/bash

apt-get update

apt-get install -y --no-install-recommends \
  make gcc \
  postgresql postgresql-server-dev-14 \
  pgxnclient

pgxn install postgresql_anonymizer

# this will emulate the `curl` commands in the how-to
cp data/* /tmp/

cp pg_hba.conf /tmp

pg_ctlcluster \
  -o "-c client_encoding='UTF8' -c shared_preload_libraries='anon' -c hba_file=/tmp/pg_hba.conf" \
  14 main restart

pg_lsclusters
