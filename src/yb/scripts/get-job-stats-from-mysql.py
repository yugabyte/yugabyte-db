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

import MySQLdb as mdb
import sys
import os

if len(sys.argv) < 3:
  sys.exit("usage: %s <job_name> <days_count_to_fetch>" % sys.argv[0])

host = os.environ["MYSQLHOST"]
user = os.environ["MYSQLUSER"]
pwd = os.environ["MYSQLPWD"]
db = os.environ["MYSQLDB"]

con = mdb.connect(host, user, pwd, db)
with con:
  cur = con.cursor()
  job_name = sys.argv[1]
  days = sys.argv[2]
  cur.execute("select workload, runtime, build_number from kudu_perf_tpch where workload like %s AND curr_date >= DATE_SUB(NOW(), INTERVAL %s DAY) and runtime != 0 ORDER BY workload, build_number, curr_date", (job_name, days))
  rows = cur.fetchall()
  print 'workload', '\t', 'runtime', '\t', 'build_number'
  for row in rows:
    print row[0], '\t', row[1], '\t', row[2]

