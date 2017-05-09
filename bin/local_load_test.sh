#!/bin/bash

set -euo pipefail

yugabyte_root=$( cd "$( dirname "$0" )"/.. && pwd )
cd "$yugabyte_root"

$yugabyte_root/bin/generic_load_test.sh 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 $@
