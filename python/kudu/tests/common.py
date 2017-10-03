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

from __future__ import division

import json
import fnmatch
import os
import shutil
import subprocess
import tempfile
import time

import kudu


class KuduTestBase(object):

    """
    Base test class that will start a configurable number of master and
    tablet servers.
    """

    BASE_PORT = 37000
    NUM_TABLET_SERVERS = 3

    @classmethod
    def start_cluster(cls):
        local_path = tempfile.mkdtemp(dir=os.getenv("TEST_TMPDIR"))
        kudu_build = os.getenv("KUDU_BUILD")
        if not kudu_build:
            kudu_build = os.path.join(os.getenv("KUDU_HOME"), "build", "latest")
        bin_path = "{0}/bin".format(kudu_build)

        os.makedirs("{0}/master/".format(local_path))
        os.makedirs("{0}/master/data".format(local_path))
        os.makedirs("{0}/master/logs".format(local_path))

        path = [
            "{0}/kudu-master".format(bin_path),
            "-rpc_server_allow_ephemeral_ports",
            "-rpc_bind_addresses=0.0.0.0:0",
            "-fs_wal_dir={0}/master/data".format(local_path),
            "-fs_data_dirs={0}/master/data".format(local_path),
            "-log_dir={0}/master/logs".format(local_path),
            "-logtostderr",
            "-webserver_port=0",
            # Only make one replica so that our tests don't need to worry about
            # setting consistency modes.
            "-default_num_replicas=1",
            "-server_dump_info_path={0}/master/config.json".format(local_path)
        ]

        p = subprocess.Popen(path, shell=False)
        fid = open("{0}/master/kudu-master.pid".format(local_path), "w+")
        fid.write("{0}".format(p.pid))
        fid.close()

        # We have to wait for the master to settle before the config file
        # appears
        config_file = "{0}/master/config.json".format(local_path)
        for i in range(30):
            if os.path.exists(config_file):
                break
            time.sleep(0.1 * (i + 1))
        else:
            raise Exception("Could not find kudu-master config file")

        # If the server was started get the bind port from the config dump
        master_config = json.load(open("{0}/master/config.json"
                                       .format(local_path), "r"))
        # One master bound on local host
        master_port = master_config["bound_rpc_addresses"][0]["port"]

        for m in range(cls.NUM_TABLET_SERVERS):
            os.makedirs("{0}/ts/{1}".format(local_path, m))
            os.makedirs("{0}/ts/{1}/logs".format(local_path, m))

            path = [
                "{0}/kudu-tserver".format(bin_path),
                "-rpc_server_allow_ephemeral_ports",
                "-rpc_bind_addresses=0.0.0.0:0",
                "-tserver_master_addrs=127.0.0.1:{0}".format(master_port),
                "-webserver_port=0",
                "-log_dir={0}/master/logs".format(local_path),
                "-logtostderr",
                "-fs_data_dirs={0}/ts/{1}/data".format(local_path, m),
                "-fs_wal_dir={0}/ts/{1}/data".format(local_path, m),
            ]
            p = subprocess.Popen(path, shell=False)
            tserver_pid = "{0}/ts/{1}/kudu-tserver.pid".format(local_path, m)
            fid = open(tserver_pid, "w+")
            fid.write("{0}".format(p.pid))
            fid.close()

        return local_path, master_port

    @classmethod
    def stop_cluster(cls, path):
        for root, dirnames, filenames in os.walk('{0}/..'.format(path)):
            for filename in fnmatch.filter(filenames, '*.pid'):
                with open(os.path.join(root, filename)) as fid:
                    a = fid.read()
                    r = subprocess.Popen(["kill", "{0}".format(a)])
                    r.wait()
                    os.remove(os.path.join(root, filename))
        shutil.rmtree(path, True)

    @classmethod
    def setUpClass(cls):
        cls.cluster_path, master_port = cls.start_cluster()
        time.sleep(1)

        cls.master_host = '127.0.0.1'
        cls.master_port = master_port

        cls.client = kudu.connect(cls.master_host, cls.master_port)

        cls.schema = cls.example_schema()

        cls.ex_table = 'example-table'
        if cls.client.table_exists(cls.ex_table):
            cls.client.delete_table(cls.ex_table)
        cls.client.create_table(cls.ex_table, cls.schema)

    @classmethod
    def tearDownClass(cls):
        cls.stop_cluster(cls.cluster_path)

    @classmethod
    def example_schema(cls):
        builder = kudu.schema_builder()
        builder.add_column('key', kudu.int32, nullable=False)
        builder.add_column('int_val', kudu.int32)
        builder.add_column('string_val', kudu.string)
        builder.set_primary_keys(['key'])

        return builder.build()
