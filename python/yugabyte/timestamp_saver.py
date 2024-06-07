# Copyright (c) Yugabyte, Inc.
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
import logging
import time

from typing import Optional, List, Dict, Tuple, Any

from yugabyte.file_util import compute_file_sha256


class TimestampSaver:
    """
    A facility for recording and restoring file modification timestamps in case the files have not
    changed, as determined using their checksums, to avoid unnecessary rebuilds.

    Usage:

    with TimestampSaver(root_dir, extension='.h') as timestamp_saver:
        # Do something that may modify files in root_dir.
    """

    file_paths: List[str]

    # Maps file path to a hash, file size, and st_mtime_ns (modification timestamp in nanoseconds)
    # obtained from os.stat.
    path_to_hash_and_stat: Dict[str, Tuple[str, int, int]]

    root_dir: str
    file_suffix: Optional[str]

    timestamp_save_overhead_sec: float

    def __init__(
            self,
            root_dir: str,
            file_suffix: Optional[str]) -> None:
        self.file_paths = []
        self.path_to_hash_and_stat = {}
        self.root_dir = root_dir
        self.file_suffix = file_suffix

    def __enter__(self) -> 'TimestampSaver':
        start_time_sec = time.time()
        self.add_files_recursively(self.root_dir, self.file_suffix)
        self.record_timestamps()
        self.timestamp_save_overhead_sec = time.time() - start_time_sec
        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.restore_timestamps()

    def add_file(self, file_path: str) -> None:
        self.file_paths.append(file_path)

    def add_files_recursively(self, dir_path: str, file_suffix: Optional[str]) -> None:
        for root, _, files in os.walk(dir_path):
            for file_name in files:
                if file_suffix is None or file_name.endswith(file_suffix):
                    file_path = os.path.join(root, file_name)
                    if not os.path.islink(file_path) and os.path.exists(file_path):
                        self.add_file(file_path)

    def compute_file_hash(self, file_path: str) -> str:
        return compute_file_sha256(file_path)

    def record_timestamps(self) -> None:
        for file_path in self.file_paths:
            stat_result = os.stat(file_path)
            self.path_to_hash_and_stat[file_path] = (
                self.compute_file_hash(file_path),
                stat_result.st_size,
                stat_result.st_mtime_ns,
            )

    def restore_timestamps(self) -> None:
        num_restored = 0
        start_time_sec = time.time()
        for file_path, hash_and_stat in self.path_to_hash_and_stat.items():
            old_hash, old_file_size, old_st_mtime_ns = hash_and_stat
            if os.path.exists(file_path):
                new_stat = os.stat(file_path)
                # Check size first before checking the hash.
                if (new_stat.st_size == old_file_size and
                        self.compute_file_hash(file_path) == old_hash and
                        new_stat.st_mtime_ns != old_st_mtime_ns):
                    num_restored += 1
                    # We rely on checking that file_path is not a symlink.
                    os.utime(file_path, ns=(new_stat.st_atime_ns, old_st_mtime_ns))

        check_restore_time_sec = time.time() - start_time_sec
        overhead_sec = check_restore_time_sec + self.timestamp_save_overhead_sec
        if num_restored > 0 or overhead_sec > 0.05:
            logging.info(
                "Saved timestamps of %d files in directory %s in %.3f sec and then checked/restored"
                " them in %.3f sec (total overhead: %.3f sec)",
                len(self.file_paths), self.root_dir, self.timestamp_save_overhead_sec,
                check_restore_time_sec, overhead_sec)
