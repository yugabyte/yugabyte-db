#!/usr/bin/env python3
# Copyright (c) YugabyteDB, Inc.
#
# Checks TCP port reachability using socket.connect_ex.
# Usage: python3 check_db_node_port_connectivity.py <host> <port>
# Exits 0 for OPEN or CLOSED (network OK); exits 1 for UNREACHABLE, FILTERED, or UNKNOWN.

import socket
import sys

TIMEOUT_SECS = {{TIMEOUT_SECS}}

if len(sys.argv) != 3:
    print("Usage: check_db_node_port_connectivity.py <host> <port>", file=sys.stderr)
    sys.exit(2)

host = sys.argv[1]
port = int(sys.argv[2])

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.settimeout(TIMEOUT_SECS)
result = s.connect_ex((host, port))
s.close()

if result == 0:
    print("OPEN", file=sys.stderr)
    sys.exit(0)
elif result in (111, 61):
    print("CLOSED - Network OK, no listener", file=sys.stderr)
    sys.exit(0)
elif result == 113:
    print("UNREACHABLE - Host down or no route", file=sys.stderr)
    sys.exit(1)
elif result in (110, 60):
    print("FILTERED - Firewall blocking", file=sys.stderr)
    sys.exit(1)
else:
    print("UNKNOWN (errno:" + str(result) + ")", file=sys.stderr)
    sys.exit(1)
