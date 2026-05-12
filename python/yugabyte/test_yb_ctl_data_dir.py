# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on the "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

"""
Unit tests for scripts/installation/bin/yb-ctl (loaded dynamically; there is no yb-ctl package).

Integration coverage lives in scripts/installation/test/yb-ctl-test.sh.
"""

import importlib.machinery
import importlib.util
import os
import pathlib
import types
import unittest
from typing import Any, ClassVar


def _repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def _load_yb_ctl_module() -> types.ModuleType:
    yb_ctl_path = _repo_root() / "scripts" / "installation" / "bin" / "yb-ctl"
    loader = importlib.machinery.SourceFileLoader("_yb_ctl_under_test", str(yb_ctl_path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    assert spec is not None
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def _minimal_update_args(**overrides: Any) -> types.SimpleNamespace:
    """Minimal kwargs for ClusterOptions.update_options_from_args (incl. install resolution)."""
    defaults = dict(
        verbose=False,
        replication_factor=1,
        binary_dir=None,
        data_dir=os.path.join(os.sep, "tmp", "yb-ctl-unit-test-data"),
        num_shards_per_tserver=2,
        ysql_num_shards_per_tserver=2,
        timeout_yb_admin_sec=45.0,
        timeout_processes_running_sec=10.0,
        install_if_needed=False,
        force=False,
        master_memory_limit_ratio=0.35,
        tserver_memory_limit_ratio=0.65,
        v="0",
        use_cassandra_authentication=False,
        ysql_hba_conf_csv=None,
        ysql_ident_conf_csv=None,
        ysql_pg_conf_csv=None,
        ip_start=1,
        listen_ip="",
        master_flags=None,
        tserver_flags=None,
        disable_ysql=False,
        enable_ysql=False,
    )
    defaults.update(overrides)
    return types.SimpleNamespace(**defaults)


class TestYbCtlDataDir(unittest.TestCase):
    yb_ctl: ClassVar[types.ModuleType]

    @classmethod
    def setUpClass(cls) -> None:
        cls.yb_ctl = _load_yb_ctl_module()

    def test_relative_data_dir_raises_exit_with_error(self) -> None:
        opts = self.yb_ctl.ClusterOptions()
        args = _minimal_update_args(data_dir="../cluster_data")
        with self.assertRaises(self.yb_ctl.ExitWithError) as ctx:
            opts.update_options_from_args(
                args, fallback_installation_dir=os.path.join(os.sep, "tmp", "yb-fake-install"))
        msg = str(ctx.exception)
        self.assertIn("must be an absolute path", msg)
        self.assertIn("fs_data_dirs", msg)
        self.assertIn("For example:", msg)
        self.assertIn("../cluster_data", msg)

    def test_absolute_data_dir_passes_validation(self) -> None:
        opts = self.yb_ctl.ClusterOptions()
        data_dir = os.path.join(os.sep, "tmp", "yb-ctl-unit-test-abs-data-dir")
        args = _minimal_update_args(data_dir=data_dir)
        opts.update_options_from_args(
            args, fallback_installation_dir=os.path.join(os.sep, "tmp", "yb-fake-install"))
        self.assertEqual(opts.cluster_base_dir, data_dir)


if __name__ == "__main__":
    unittest.main()
