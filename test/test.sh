#!/bin/bash
set -e

# Install pgaudit so postgres will start with shared_preload_libraries set
sudo make -C /pgaudit clean install USE_PGXS=1

# Start postgres
${PGBIN}/pg_ctl -w start -D ${PGDATA}

# Test pgaudit
make -C /pgaudit installcheck USE_PGXS=1
