#!/usr/bin/env python3
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
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
Regression test: `yugabyted status` must bound its probe of the yugabyted-ui
endpoint. A stopped yugabyted-ui process accepts TCP connections but never
answers, and an untimed urlopen() then blocks the status command forever.
"""

import importlib.machinery
import importlib.util
import os
import unittest
from unittest import mock

YUGABYTED_UI_PORT = 19999
YUGABYTED_SCRIPT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..', 'bin', 'yugabyted')


def load_yugabyted_module():
    loader = importlib.machinery.SourceFileLoader('yugabyted_script', YUGABYTED_SCRIPT)
    spec = importlib.util.spec_from_loader('yugabyted_script', loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


class YugabytedStatusUiTimeoutTest(unittest.TestCase):
    def test_ui_probe_passes_timeout(self):
        module = load_yugabyted_module()

        ui_probe_kwargs = []

        def fake_urlopen(req, *args, **kwargs):
            if ':{}'.format(YUGABYTED_UI_PORT) in req.full_url:
                ui_probe_kwargs.append(kwargs)
            # Fail the probe so status falls back to the master web console.
            raise OSError('simulated unreachable UI')

        self_mock = mock.MagicMock()
        self_mock.configs.saved_data = {
            'ui': True,
            'yugabyted_ui_port': YUGABYTED_UI_PORT,
            'webserver_port': 15000,
            'master_webserver_port': 7000,
            'ycql_port': 9042,
            'ysql_port': 5433,
        }
        self_mock.configs.temp_data = {'ui_port_available': True}
        self_mock.advertise_ip.return_value = '127.0.0.1'
        self_mock.get_current_masters_from_api.return_value = ['127.0.0.1']
        self_mock.setup_env_init.get_ycql_credentials.return_value = ('', '', '')
        self_mock.setup_env_init.get_ysql_credentials.return_value = ('', '', '')

        with mock.patch.object(module, 'urlopen', side_effect=fake_urlopen):
            module.ControlScript.get_status_string(self_mock)

        self.assertEqual(len(ui_probe_kwargs), 1, 'UI probe was not attempted')
        self.assertIn('timeout', ui_probe_kwargs[0],
                      'UI status probe was called without a timeout; '
                      'it can block `yugabyted status` forever')
        self.assertGreater(ui_probe_kwargs[0]['timeout'], 0)


if __name__ == '__main__':
    unittest.main()
