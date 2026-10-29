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

"""Tests for did_test_succeed in build-support/common-test-env.sh."""

import pathlib
import subprocess

import pytest


YB_SRC_ROOT = pathlib.Path(__file__).resolve().parents[2]


def did_test_succeed(log_text: str, tmp_path: pathlib.Path) -> bool:
    log_path = tmp_path / 'test.log'
    log_path.write_text(log_text, encoding='utf-8')
    result = subprocess.run(
        ['bash', '-c',
         '. build-support/common-test-env.sh >/dev/null 2>&1; '
         'did_test_succeed 0 "$1" && echo SUCCEEDED || echo FAILED',
         'bash', str(log_path)],
        cwd=YB_SRC_ROOT, capture_output=True, text=True, check=True)
    return result.stdout.strip().splitlines()[-1] == 'SUCCEEDED'


def test_clean_log(tmp_path: pathlib.Path) -> None:
    assert did_test_succeed('[       OK ] Foo.Bar (1 ms)\n', tmp_path)


@pytest.mark.parametrize('report', [
    'heap-use-after-free on address 0x60200000eff0',
    'heap-buffer-overflow on address 0x602000000050',
    'stack-buffer-overflow on address 0x7ffd4a3b8a20',
    'stack-use-after-return on address 0x7f5f1d5f3020',
    'attempting double-free on 0x602000000010 in thread T0:',
    'SEGV on unknown address 0x000000000000',
    'use-after-poison on address 0x631000010800',
])
def test_address_sanitizer_report_fails_test(report: str, tmp_path: pathlib.Path) -> None:
    assert not did_test_succeed(f'ts1|pid5| ==123==ERROR: AddressSanitizer: {report}\n', tmp_path)


@pytest.mark.parametrize('report', [
    'WARNING:  detected write past chunk end in ExprContext 0x7dbe9d830210',
    'WARNING:  problem in alloc set ExprContext: bogus aset link in block 0x1, chunk 0x2',
    'WARNING:  problem in slab Change: bogus slab link in block 0x1, chunk 0x2',
    'WARNING:  problem in Generation Tuples: bogus chunk size in block 0x1, chunk 0x2',
    # PG code in the tserver (ybgate) logs the report at INFO severity, with no WARNING prefix.
    'I0930 17:40:02.421000 594514 aset.c:1153] detected write past chunk end in Ybg 0x1',
])
def test_memory_context_check_report_fails_test(report: str, tmp_path: pathlib.Path) -> None:
    assert not did_test_succeed(f'ts1|pid5| {report}\n', tmp_path)
