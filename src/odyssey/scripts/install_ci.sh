#!/usr/bin/env bash

set -ex

sudo sh -c 'echo "deb http://apt.postgresql.org/pub/repos/apt $(lsb_release -cs)-pgdg main" > /etc/apt/sources.list.d/pgdg.list'
wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | sudo apt-key add -
sudo apt-get update
sudo apt-get -y --no-install-recommends install postgresql-14 postgresql-server-dev-14 libpq5 libpq-dev clang-format-10 libpam0g-dev libldap-dev
sudo pkill -9 postgres || true
cho -n | openssl s_client -connect https://scan.coverity.com:443 | sed -ne '/-BEGIN CERTIFICATE-/,/-END CERTIFICATE-/p' | sudo tee -a /etc/ssl/certs/ca-
