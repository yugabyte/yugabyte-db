#!/usr/bin/python
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Converts glog output from wal_hiccup into a CSV of all test runs.
#
# The output looks like this:
#
# var1,var2,...,varN,throughput,p95,p99,p99.99,max
# 0,0,...,68.5702,29,8544,22263,29108
#
# Each variable represents a different facet of the test setup.
# The throughput is in MB/s and the latency figures are in us.

import re
import sys

def main():
    if len(sys.argv) != 2:
        print "Usage: %s <log output>" % (sys.argv[0],)
        return
    cols = list()
    cols_printed = False
    with open(sys.argv[1], "r") as f:
        vals = None
        for line in f:
            line = line.strip()

            # Beginning of a test result.
            if "Test results for setup" in line:
                vals = list()

            # End of a test result.
            elif "-------" in line and vals is not None:
                if not cols_printed:
                    print ",".join(cols)
                    cols_printed = True
                print ",".join(vals)
                vals = None

            # Entry in a test result.
            elif vals is not None:
                m = re.match(".*\] ([\w\.]+): ([\d\.]+)", line)
                if not m:
                    continue
                col = m.group(1)
                val = m.group(2)
                if cols_printed:
                    if col not in cols:
                        raise Exception("Unexpected column %s" % (col,))
                else:
                    cols.append(col)
                vals.append(val)

if __name__ == '__main__':
    main()
