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

import os
import pathlib
import signal
import subprocess
import sys
import threading
import time
from typing import Callable, List, Optional

import psutil
import pytest

from yugabyte import process_tree_supervisor


PROCESS_TREE_SCRIPT = r'''
import os
import pathlib
import signal
import sys
import time

state_dir = pathlib.Path(sys.argv[1])


def write_pid(name, pid):
    (state_dir / name).write_text(str(pid))


def wait_for(name):
    while not (state_dir / name).exists():
        time.sleep(0.01)


child_pid = os.fork()
if child_pid:
    while True:
        signal.pause()

write_pid('child.pid', os.getpid())
wait_for('spawn-grandchild')
grandchild_pid = os.fork()
if grandchild_pid:
    while True:
        signal.pause()

write_pid('grandchild.pid', os.getpid())
wait_for('spawn-descendant')
intermediate_pid = os.fork()
if intermediate_pid:
    while True:
        signal.pause()

os.setsid()
descendant_pid = os.fork()
if descendant_pid:
    write_pid('intermediate.pid', os.getpid())
    write_pid('descendant.pid', descendant_pid)
    wait_for('reparent-descendant')
    os._exit(0)

while True:
    signal.pause()
'''


def wait_for(condition: Callable[[], bool], timeout_sec: float = 10) -> None:
    deadline = time.monotonic() + timeout_sec
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.05)
    pytest.fail('Timed out waiting for process-tree state')


def log_contains(log_path: pathlib.Path, message: str) -> bool:
    return log_path.exists() and message in log_path.read_text()


def read_pid(path: pathlib.Path) -> int:
    wait_for(path.exists)
    return int(path.read_text())


def process_is_gone(pid: int) -> bool:
    try:
        return psutil.Process(pid).status() == psutil.STATUS_ZOMBIE
    except psutil.NoSuchProcess:
        return True


def kill_processes(pids: List[int]) -> None:
    for pid in reversed(pids):
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def test_tracks_reports_and_terminates_reparented_descendant(tmp_path: pathlib.Path) -> None:
    tree_process: Optional[subprocess.Popen] = None
    supervisor_process: Optional[subprocess.Popen] = None
    spawned_pids: List[int] = []
    log_path = tmp_path / 'supervisor.log'

    try:
        tree_process = subprocess.Popen(
            [sys.executable, '-c', PROCESS_TREE_SCRIPT, str(tmp_path)])
        spawned_pids.append(tree_process.pid)

        supervisor_process = subprocess.Popen([
            sys.executable,
            str(pathlib.Path(process_tree_supervisor.__file__)),
            '--pid', str(tree_process.pid),
            '--terminate-subtree',
            '--log-to-file', str(log_path),
        ])

        child_pid = read_pid(tmp_path / 'child.pid')
        spawned_pids.append(child_pid)
        wait_for(lambda: log_contains(log_path, 'Tracking a child pid: %d' % child_pid))

        (tmp_path / 'spawn-grandchild').touch()
        grandchild_pid = read_pid(tmp_path / 'grandchild.pid')
        spawned_pids.append(grandchild_pid)
        wait_for(lambda: log_contains(log_path, 'Tracking a child pid: %d' % grandchild_pid))

        (tmp_path / 'spawn-descendant').touch()
        intermediate_pid = read_pid(tmp_path / 'intermediate.pid')
        descendant_pid = read_pid(tmp_path / 'descendant.pid')
        spawned_pids.extend([intermediate_pid, descendant_pid])
        wait_for(lambda: log_contains(log_path, 'Tracking a child pid: %d' % intermediate_pid))
        wait_for(lambda: log_contains(log_path, 'Tracking a child pid: %d' % descendant_pid))

        (tmp_path / 'reparent-descendant').touch()
        wait_for(lambda: process_is_gone(intermediate_pid))
        wait_for(lambda: psutil.Process(descendant_pid).ppid() != intermediate_pid)
        descendant_pids = {child.pid for child in psutil.Process(tree_process.pid).children(
            recursive=True)}
        assert descendant_pid not in descendant_pids

        supervisor_process.send_signal(signal.SIGUSR1)
        assert supervisor_process.wait(timeout=10) == 0
        log_contents = log_path.read_text()
        stray_pids = [child_pid, grandchild_pid, descendant_pid]
        for pid in stray_pids:
            assert 'YB_STRAY_PROCESS: Found a stray process, killing: pid: %d' % pid in log_contents
        wait_for(lambda: all(process_is_gone(pid) for pid in stray_pids))
    finally:
        if supervisor_process is not None and supervisor_process.poll() is None:
            supervisor_process.kill()
            supervisor_process.wait()

        cleanup_pids = spawned_pids.copy()
        for pid_path in tmp_path.glob('*.pid'):
            try:
                cleanup_pids.append(int(pid_path.read_text()))
            except (OSError, ValueError):
                pass
        if tree_process is not None and tree_process.poll() is None:
            try:
                descendants = psutil.Process(tree_process.pid).children(recursive=True)
                cleanup_pids.extend(child.pid for child in descendants)
            except psutil.NoSuchProcess:
                pass
        kill_processes(cleanup_pids)
        if tree_process is not None:
            tree_process.wait()


PROC_CHILDREN_AVAILABLE = (
    process_tree_supervisor.get_direct_child_pids(os.getpid()) is not None)
PROC_CHILDREN_SKIP = pytest.mark.skipif(
    not PROC_CHILDREN_AVAILABLE, reason='procfs children data is unavailable')


@PROC_CHILDREN_SKIP
def test_reads_direct_children_from_proc() -> None:
    child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])
    try:
        child_pids = process_tree_supervisor.get_direct_child_pids(os.getpid())
        assert child_pids is not None
        assert child.pid in child_pids
    finally:
        child.kill()
        child.wait()


@PROC_CHILDREN_SKIP
def test_reads_child_started_by_non_leader_thread() -> None:
    children: List[subprocess.Popen] = []
    child_started = threading.Event()
    stop_thread = threading.Event()

    def start_child() -> None:
        children.append(subprocess.Popen([
            sys.executable, '-c', 'import time; time.sleep(60)']))
        child_started.set()
        stop_thread.wait()

    thread = threading.Thread(target=start_child)
    thread.start()
    try:
        assert child_started.wait(timeout=10)
        child = children[0]
        child_pids = process_tree_supervisor.get_direct_child_pids(os.getpid())
        assert child_pids is not None
        assert child.pid in child_pids
    finally:
        stop_thread.set()
        thread.join()
        for child in children:
            child.kill()
            child.wait()
