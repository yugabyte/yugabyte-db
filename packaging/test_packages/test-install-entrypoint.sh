#!/bin/bash
set -e

# Change to the test directory
cd /test-install

# Keep the internal directory out of the testing
sed -i '/internal/d' Makefile

# Run the test
adduser --disabled-password --gecos "" heliodb
chown -R heliodb:heliodb .
su heliodb -c "make check"
