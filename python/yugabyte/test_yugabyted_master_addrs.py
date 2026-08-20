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
Unit tests for master address handling in bin/yugabyted (loaded dynamically; there is no
yugabyted package).

An empty entry in current_masters used to reach yb-tserver as --tserver_master_addrs=,host:7100.
The tserver then retried DNS on the empty host inside ResolveMasterAddresses() for
master_discovery_timeout_ms (1 hour by default) before logging anything past "Initializing tablet
server...", and yugabyted persisted the bad value, so every later start hung the same way.

Integration coverage lives in scripts/yugabyted/test/yugabyted-test.sh.
"""

import importlib.machinery
import importlib.util
import json
import os
import pathlib
import tempfile
import types
import unittest
from typing import Any, ClassVar, Dict


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


class TestParseMasterAddrsCsv(unittest.TestCase):
    yugabyted: ClassVar[types.ModuleType]

    @classmethod
    def setUpClass(cls) -> None:
        cls.yugabyted = _load_yugabyted_module()

    def test_empty_entries_are_dropped(self) -> None:
        parse = self.yugabyted.parse_master_addrs_csv
        self.assertEqual(parse(",host:7100"), ["host:7100"])
        self.assertEqual(parse("a:7100,,b:7100"), ["a:7100", "b:7100"])
        self.assertEqual(parse(" a:7100 , b:7100 "), ["a:7100", "b:7100"])

    def test_no_addresses_gives_empty_list(self) -> None:
        parse = self.yugabyted.parse_master_addrs_csv
        # "".split(",") returns [""], which is truthy and passes an "if not list" guard.
        self.assertEqual(parse(""), [])
        self.assertEqual(parse(None), [])

    def test_populated_list_is_unchanged(self) -> None:
        parse = self.yugabyted.parse_master_addrs_csv
        self.assertEqual(parse("a:7100,b:7100"), ["a:7100", "b:7100"])


class TestConfigMasterAddrsRecovery(unittest.TestCase):
    """parse_config_file() should drop a host-less entry left behind by an older version."""

    yugabyted: ClassVar[types.ModuleType]

    @classmethod
    def setUpClass(cls) -> None:
        cls.yugabyted = _load_yugabyted_module()

    def _load_saved_masters(self, saved_data: Dict[str, Any]) -> Any:
        with tempfile.TemporaryDirectory() as tmp_dir:
            config_file = os.path.join(tmp_dir, "yugabyted.conf")
            with open(config_file, "w") as f:
                json.dump(saved_data, f)
            configs = self.yugabyted.Configs.parse_config_file(config_file, tmp_dir)
            return configs.saved_data.get("current_masters")

    def test_host_less_entry_is_dropped_on_load(self) -> None:
        self.assertEqual(
            self._load_saved_masters({"current_masters": ",host:7100"}), "host:7100")

    def test_valid_list_is_left_alone(self) -> None:
        self.assertEqual(
            self._load_saved_masters({"current_masters": "a:7100,b:7100"}), "a:7100,b:7100")

    def test_empty_value_stays_empty(self) -> None:
        self.assertEqual(self._load_saved_masters({"current_masters": ""}), "")


class TestUpdateTserverMasterAddrs(unittest.TestCase):
    """update_tserver_master_addrs() should never build a flag with a host-less address."""

    yugabyted: ClassVar[types.ModuleType]

    @classmethod
    def setUpClass(cls) -> None:
        cls.yugabyted = _load_yugabyted_module()

    def _run_update(self, current_masters: str) -> types.SimpleNamespace:
        script = self.yugabyted.ControlScript.__new__(self.yugabyted.ControlScript)
        tserver_cmd = ["/home/yugabyte/bin/yb-tserver", "--tserver_master_addrs=host:7100"]
        script.processes = {"tserver": types.SimpleNamespace(cmd=tserver_cmd)}
        script.configs = types.SimpleNamespace(saved_data={
            "current_masters": current_masters,
            "master_rpc_port": 7100,
        })
        script.advertise_ip = lambda: "host"
        script.update_tserver_master_addrs()
        return types.SimpleNamespace(
            cmd=tserver_cmd, saved_data=script.configs.saved_data)

    def test_empty_current_masters_keeps_existing_flag(self) -> None:
        result = self._run_update("")
        self.assertEqual(
            [arg for arg in result.cmd if arg.startswith("--tserver_master_addrs=")],
            ["--tserver_master_addrs=host:7100"])

    def test_host_less_entry_is_not_passed_to_tserver(self) -> None:
        result = self._run_update(",host:7100")
        flags = [arg for arg in result.cmd if arg.startswith("--tserver_master_addrs=")]
        self.assertEqual(flags, ["--tserver_master_addrs=host:7100"])
        self.assertEqual(result.saved_data["current_masters"], "host:7100")

    def test_multiple_masters_are_preserved(self) -> None:
        result = self._run_update("a:7100,b:7100")
        flags = [arg for arg in result.cmd if arg.startswith("--tserver_master_addrs=")]
        self.assertEqual(len(flags), 1)
        addrs = flags[0].split("=", 1)[1].split(",")
        self.assertEqual(sorted(addrs), ["a:7100", "b:7100", "host:7100"])


if __name__ == "__main__":
    unittest.main()
