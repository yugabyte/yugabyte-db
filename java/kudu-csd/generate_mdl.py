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
# Script to generate a CM-compatible MDL file from the metrics
# metadata dumped by our daemon processes.
#
# Requires that the daemon processes have already been built and available
# in the build/latest/bin directory.
#
# Outputs the MDL file on stdout by default or to a file specified in the first
# argument.

import collections
try:
  import simplejson as json
except:
  import json
import os
import subprocess
import sys

BINARIES=["kudu-master", "kudu-tserver"]

RELATIVE_BUILD_DIR="../../build/latest/bin"

def find_binary(bin_name):
  dirname, _ = os.path.split(os.path.abspath(__file__))
  build_dir = os.path.join(dirname, RELATIVE_BUILD_DIR)
  path = os.path.join(build_dir, bin_name)
  if os.path.exists(path):
    return path
  raise Exception("Cannot find %s in build dir %s" % (bin_name, build_dir))

def load_all_metrics():
  """
  For each binary, dump and parse its metrics schema by running it with
  the --dump_metrics_json flag.
  """
  all_metrics = []
  for binary in BINARIES:
    binary = find_binary(binary)
    p = subprocess.Popen([binary, "--dump_metrics_json"],
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    stdout, stderr = p.communicate()
    rc = p.returncode

    if rc != 0:
      print >>sys.stderr, "error: %s exited with return code %d:\n" % (binary, rc)
      print >>sys.stderr, stderr
      sys.exit(1)

    metrics_dump = json.loads(stdout)
    all_metrics.extend(m for m in metrics_dump['metrics'])
  return all_metrics

def append_sentence(a, b):
  if not a.endswith("."):
    a += "."
  return a + " " + b

def metric_to_mdl_entries(m):
  if m['type'] == 'histogram':
    return [
      dict(
        context=(m['name'] + "::total_count"),
        name=('kudu_' + m['name'].lower()) + '_count',
        counter=True,
        numeratorUnit='samples',
        description=append_sentence(m['description'],
                                    "This is the total number of recorded samples."),
        label=m['label'] + ": Samples"),
      dict(
        context=(m['name'] + "::total_sum"),
        name=('kudu_' + m['name'].lower()) + '_sum',
        counter=True,
        numeratorUnit=m['unit'],
        description=append_sentence(m['description'],
                                    "This is the total sum of recorded samples."),
        label=m['label'] + ": Total")
      ]

  return [dict(
    context=(m['name'] + "::value"),
    name=('kudu_' + m['name'].lower()),
    counter=(m['type'] == 'counter'),
    numeratorUnit=m['unit'],
    description=m['description'],
    label=m['label'])]

def metrics_to_mdl(metrics):
  """
  For each metric returned by the daemon, convert it to the MDL-compatible dictionary.
  Returns a map of entity_type_name -> [metric dicts].
  """
  seen = set()

  by_entity = collections.defaultdict(lambda: [])
  for m in metrics:
    # Don't process any metric more than once. Some metrics show up
    # in both daemons.
    key = (m['entity_type'], m['name'])
    if key in seen:
      continue
    seen.add(key)

    # Convert to the format that CM expects.
    by_entity[m['entity_type']].extend(metric_to_mdl_entries(m))
  return by_entity

def main():
  all_metrics = load_all_metrics()
  metrics_by_entity = metrics_to_mdl(all_metrics)
  server_metrics = metrics_by_entity['server']
  tablet_metrics = metrics_by_entity['tablet']

  output = dict(
    name="KUDU",
    version="0.6.0",
    metricDefinitions=[],
    nameForCrossEntityAggregateMetrics="kudus",
    roles=[
      dict(name="KUDU_TSERVER",
           nameForCrossEntityAggregateMetrics="kudu_tservers",
           metricDefinitions=server_metrics),
      dict(name="KUDU_MASTER",
           nameForCrossEntityAggregateMetrics="kudu_masters",
           metricDefinitions=server_metrics)
      ],
    metricEntityAttributeDefinitions=[
      dict(name="kuduTableId",
           label="Table ID",
           description="UUID for Kudu Table.",
           valueCaseSensitive=False),
      dict(name="kuduTableName",
           label="Table Name",
           description="Name for Kudu Table.",
           valueCaseSensitive=True),
      dict(name="kuduTableState",
           label="Table State",
           description="State for Kudu Table.",
           valueCaseSensitive=False),
      dict(name="kuduTabletId",
           label="Tablet ID",
           description="UUID for Kudu Tablet.",
           valueCaseSensitive=False),
      dict(name="kuduTabletState",
           label="Tablet State",
           description="State for Kudu Tablet.",
           valueCaseSensitive=False)
      # TODO: add the role's persistent UUID after discussing with
      # Chris on how to inject it into their CM entity.
      ],
    metricEntityTypeDefinitions=[
      dict(name="KUDU_TABLE",
           nameForCrossEntityAggregateMetrics="kudu_tables",
           immutableAttributeNames=["serviceName", "kuduTableId"],
           mutableAttributeNames=["kuduTableName", "kuduTableState"],
           entityNameFormat=["serviceName", "kuduTableId"],
           description="A Kudu table.",
           label="Kudu Table",
           labelPlural="Kudu Tables",
           entityLabelFormat="$kuduTableName ($serviceDisplayName)",
           parentMetricEntityTypeNames=["KUDU"],
           metricDefinitions=[]),
      dict(name="KUDU_TABLET",
           nameForCrossEntityAggregateMetrics="kudu_tablets",
           immutableAttributeNames=["serviceName", "kuduTableId", "kuduTabletId"],
           mutableAttributeNames=["kuduTabletState"],
           entityNameFormat=["serviceName", "kuduTabletId"],
           description="A Kudu tablet.",
           label="Kudu Tablet",
           labelPlural="Kudu Tablets",
           entityLabelFormat="$kuduTabletId ($kuduTableName) ($serviceDisplayName)",
           parentMetricEntityTypeNames=["KUDU_TABLE"],
           metricDefinitions=[]),
      dict(name="KUDU_REPLICA",
           nameForCrossEntityAggregateMetrics="kudu_replicas",
           immutableAttributeNames=["kuduTabletId", "serviceName", "roleName"],
           entityNameFormat=["roleName","kuduTabletId"],
           description="A Kudu replica.",
           label="Kudu Replica",
           labelPlural="Kudu Replicas",
           entityLabelFormat="$kuduTabletId ($kuduTableName) ($hostname)",
           parentMetricEntityTypeNames=["KUDU_TABLET","KUDU-KUDU_TSERVER"],
           metricDefinitions=tablet_metrics),
      ])

  
  f = sys.stdout
  if len(sys.argv) > 1:
    f = open(sys.argv[1], 'w')
  f.write(json.dumps(output, indent=4))

if __name__ == "__main__":
  main()
