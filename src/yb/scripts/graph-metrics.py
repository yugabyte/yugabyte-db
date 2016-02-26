#!/usr/bin/env python
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
# Script which parses a test log for 'metrics: ' lines emited by
# TimeSeriesCollector, and constructs a graph from them

import os
import re
import simplejson
import sys

METRICS_LINE = re.compile('metrics: (.+)$')
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

def parse_data_from(stream, scope):
  data = []
  scanned = 0
  prev_time = 0
  for line in stream:
    if 'metrics: {' not in line:
      continue
    match = METRICS_LINE.search(line)
    if not match:
      continue
    json = match.group(1)
    try:
      data_points = simplejson.loads(json)
    except:
      print >>sys.stderr, "bad json:", json
      raise
    if data_points['scope'] != scope:
      continue
    del data_points['scope']
    if 'scan_rate' in data_points:
      scanned += (data_points['scan_rate'] * (data_points['time'] - prev_time))
      data_points['scanned'] = scanned
      del data_points['scan_rate']
    prev_time = data_points['time']

    data.append(data_points)
  return data


def get_keys(raw_data):
  keys = set()
  for row in raw_data:
    keys.update(row.keys())
  return keys

def main():
  scope = sys.argv[1]
  data = parse_data_from(sys.stdin, scope)
  keys = get_keys(data)

  with sys.stdout as f:
    print >>f, "\t".join(keys)
    for row in data:
      print >>f, "\t".join([str(row.get(k, 0)) for k in keys])


if __name__ == "__main__":
  main()
