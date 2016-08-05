#!/bin/bash

set -euo pipefail

yugabyte_root=$( cd "$( dirname "$0" )"/.. && pwd )
cd "$yugabyte_root"

$yugabyte_root/bin/generic_load_test.sh localhost:7101,localhost:7102,localhost:7103 $@
