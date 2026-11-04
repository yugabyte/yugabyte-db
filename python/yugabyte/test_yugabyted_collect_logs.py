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
Unit tests for `yugabyted collect_logs` in bin/yugabyted (loaded dynamically; there is no
yugabyted package).
"""

import importlib.machinery
import importlib.util
import io
import os
import pathlib
import tarfile
import tempfile
import types
import unittest
import zlib
from typing import ClassVar, Tuple
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


class TestYugabytedCollectLogs(unittest.TestCase):
    yugabyted: ClassVar[types.ModuleType]

    @classmethod
    def setUpClass(cls) -> None:
        cls.yugabyted = _load_yugabyted_module()

    def _collect_logs(self, to_stdout: bool) -> Tuple[bytes, str, str]:
        with tempfile.TemporaryDirectory() as log_dir, \
                tempfile.TemporaryDirectory() as collect_at_dir:
            with open(os.path.join(log_dir, "yugabyted.log"), "w") as f:
                f.write("log line\n")
            cs = self.yugabyted.ControlScript()
            cs.configs = types.SimpleNamespace(
                saved_data={"log_dir": log_dir},
                temp_data={"collect_at_dir": collect_at_dir, "collect_logs_stdout": to_stdout})
            stdout_bytes = io.BytesIO()
            fake_stdout = io.TextIOWrapper(stdout_bytes, write_through=True)
            fake_stderr = io.StringIO()
            with mock.patch("sys.stdout", fake_stdout), mock.patch("sys.stderr", fake_stderr), \
                    self.assertLogs(level="INFO") as logs:
                cs.collect_logs()
                fake_stdout.flush()
            return stdout_bytes.getvalue(), fake_stderr.getvalue(), "\n".join(logs.output)

    def test_stdout_holds_only_the_archive(self) -> None:
        stdout, stderr, log = self._collect_logs(to_stdout=True)
        decompressor = zlib.decompressobj(zlib.MAX_WBITS | 16)
        tar_bytes = decompressor.decompress(stdout)
        self.assertEqual(decompressor.unused_data, b"")
        with tarfile.open(fileobj=io.BytesIO(tar_bytes)) as archive:
            self.assertEqual(archive.getnames(), ["yugabyted.log"])
        self.assertIn("Logs collected successfully.", stderr)
        self.assertIn("Logs collected successfully.", log)

    def test_status_goes_to_stdout_without_the_flag(self) -> None:
        stdout, stderr, log = self._collect_logs(to_stdout=False)
        self.assertIn("Logs collected successfully.", stdout.decode())
        self.assertEqual(stderr, "")
        self.assertIn("Logs collected successfully.", log)


if __name__ == "__main__":
    unittest.main()
