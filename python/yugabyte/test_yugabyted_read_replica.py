# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

"""
Unit tests for the read replica supervision loop in bin/yugabyted (loaded dynamically; there is no
yugabyted package).
"""

import importlib.machinery
import importlib.util
import pathlib
import tempfile
import types
import unittest
from typing import Any, ClassVar, List
from unittest import mock


def _repo_root() -> pathlib.Path:
    return pathlib.Path(__file__).resolve().parents[2]


def _load_yugabyted_module() -> types.ModuleType:
    yugabyted_path = _repo_root() / "bin" / "yugabyted"
    loader = importlib.machinery.SourceFileLoader("_yugabyted_under_test", str(yugabyted_path))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    assert spec is not None
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


class _StopLoop(Exception):
    pass


class _DeadProcess:
    """Stands in for YBProcessManager; the process always reports that it is not running."""

    def __init__(self, name: str, *args: Any) -> None:
        self.name = name
        self.start_count = 0

    def get_pid(self) -> None:
        return None

    def remove_error_logs(self) -> None:
        pass

    def is_running(self) -> bool:
        return False

    def start(self) -> None:
        self.start_count += 1


class TestYugabytedReadReplica(unittest.TestCase):
    yugabyted: ClassVar[types.ModuleType]

    @classmethod
    def setUpClass(cls) -> None:
        cls.yugabyted = _load_yugabyted_module()

    def _make_control_script(self, data_dir: str) -> Any:
        yb = self.yugabyted
        cs = yb.ControlScript()
        cs.configs = types.SimpleNamespace(
            saved_data={
                "master_rpc_port": 7100, "join": None, "upgrade_in_progress": "false",
                "data_dir": data_dir, "log_dir": data_dir, "polling_interval": 0,
                "callhome": False, "secure": True,
            },
            temp_data={},
            save_configs=lambda: None)
        cs.script = types.SimpleNamespace(set_rlimits=lambda print_info: [], pidfile="pid")
        cs.alerts = []
        cs.setup_env_init = types.SimpleNamespace(get_ysql_password=lambda: "")
        stubs = {
            "prereqs_check": {"status": yb.Output.ANIMATION_SUCCESS, "msg": ""},
            "get_common_flags": [],
            "get_tserver_cmd": ["true"],
            "is_yb_initialized": True,
            "post_install_yb": None,
            "start_first_read_replica_tserver": None,
            "get_status_string": ("", 0),
            "update_masters_list_loop": None,
            "update_tserver_master_addrs": None,
        }
        for name, value in stubs.items():
            setattr(cs, name, (lambda v: (lambda *args, **kwargs: v))(value))
        return cs

    def test_tserver_restart_keeps_supervision_loop_running(self) -> None:
        yb = self.yugabyted
        sleeps: List[int] = []

        def fake_sleep(_: float) -> None:
            sleeps.append(1)
            if len(sleeps) == 2:
                raise _StopLoop()

        with tempfile.TemporaryDirectory() as data_dir:
            cs = self._make_control_script(data_dir)
            with mock.patch.object(yb.Output, "console_access", False), \
                    mock.patch.object(yb, "YBProcessManager", _DeadProcess), \
                    mock.patch.object(yb.time, "sleep", fake_sleep):
                with self.assertRaises(_StopLoop):
                    cs.start_rr_process()
        self.assertEqual(cs.processes["tserver"].start_count, 1)


if __name__ == "__main__":
    unittest.main()
