#!/bin/bash
set -e

# Clean and build pgaudit
make -C /pgaudit clean all USE_PGXS=1

# Install pgaudit so postgres will start with shared_preload_libraries set
sudo bash -c "PATH=${PATH?} make -C /pgaudit install USE_PGXS=1"

# Start postgres
${PGBIN}/pg_ctl -w start -D ${PGDATA}

# Test pgaudit
make -C /pgaudit installcheck USE_PGXS=1
