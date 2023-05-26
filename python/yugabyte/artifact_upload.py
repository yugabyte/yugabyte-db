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
#

import dataclasses
import socket
import logging
import subprocess
import os
import time

from enum import Enum

from yugabyte import file_util
from yugabyte.common_util import is_macos

from typing import List, Tuple, Callable, Set, Optional, Union


DEFAULT_UPLOAD_RETRY_INITIAL_DELAY_SEC = 0.2
DEFAULT_UPLOAD_RETRY_DELAY_MULTIPLIER = 1.5
DEFAULT_MAX_ATTEMPTS_PER_FILE = 4


class UploadMethod(Enum):
    SSH = 'SSH'
    CP = 'CP'

    @staticmethod
    def from_env() -> 'UploadMethod':
        if os.getenv('YB_SPARK_COPY_MODE') == 'SSH':
            return UploadMethod.SSH
        return UploadMethod.CP


@dataclasses.dataclass
class FileTransferResult:
    """
    Results of copying a number of files, typically from the test runner machine to the main Jenkins
    worker machine that is running the build.
    """

    files_copied: List[Tuple[str, int]] = dataclasses.field(default_factory=list)

    # Files that were found but not copied, due to an error, and their sizes.
    files_not_copied_due_to_errors: List[Tuple[str, int]] = dataclasses.field(default_factory=list)

    # Files that were found but not copied, due to a size limit, and their sizes.
    files_too_large: List[Tuple[str, int]] = dataclasses.field(default_factory=list)

    # The files that were not present on the test node. Indicates an error in the logic that
    # collects the list of test artifacts.
    files_not_found: List[str] = dataclasses.field(default_factory=list)

    # Total time taken by the copying.
    total_time_sec: float = 0.0

    # Total time waiting before retries.
    total_wait_time_sec: float = 0.0

    # Number of errors encountered when copying files. Some of these errors might have been retried
    # during later attempts.
    num_copy_errors: int = 0

    exception_traceback: Optional[str] = None

    cached_common_parent_dir: Optional[str] = None

    def num_files_copied(self) -> int:
        return len(self.files_copied)

    def num_files_not_copied(self) -> int:
        return (len(self.files_not_copied_due_to_errors) +
                len(self.files_too_large) +
                len(self.files_not_found))

    def all_files(self) -> Set[str]:
        return set(
            [f[0] for f in self.files_copied] +
            [f[0] for f in self.files_not_copied_due_to_errors] +
            [f[0] for f in self.files_too_large] +
            self.files_not_found)

    def common_parent_dir(self) -> str:
        """
        Returns the common parent directory that all the files are in, or the directory of the
        single file if there is only one file.
        """
        if self.cached_common_parent_dir is not None:
            return self.cached_common_parent_dir
        all_files = [os.path.abspath(file_path) for file_path in self.all_files()]
        assert all_files, "Cannot determine common parent directory: no files"
        if len(all_files) == 1:
            self.cached_common_parent_dir = os.path.dirname(all_files[0])
        else:
            self.cached_common_parent_dir = os.path.commonpath(all_files)
        return self.cached_common_parent_dir

    def _format_file_list(
            self,
            file_list: Union[List[str], List[Tuple[str, int]]],
            indent: int = 4) -> List[str]:
        common_parent_dir = self.common_parent_dir()
        result = []
        for file_path_and_maybe_size in file_list:
            file_path: str
            file_size: Optional[int] = None
            # Using "type: ignore" to avoid these errors with some versions of mypy:
            # - Incompatible types in assignment (expression has type "object", variable has
            # type "str")
            # - Incompatible types in assignment (expression has type "object", variable has type
            # "Optional[int]")
            if isinstance(file_path_and_maybe_size, str):
                file_path = file_path_and_maybe_size  # type: ignore
            else:
                file_path, file_size = file_path_and_maybe_size  # type: ignore
            if common_parent_dir != '/':
                file_path = os.path.relpath(file_path, common_parent_dir)
            line = ' ' * indent + file_path
            if file_size is not None:
                line += f" ({file_size} bytes)"
            result.append(line)
        return result

    def __str__(self) -> str:
        lines = ['FileTransferResult:']
        common_parent_dir = self.common_parent_dir()
        if common_parent_dir != '/':
            lines.append(f"Common parent directory: {common_parent_dir}")

        def add_file_list(title: str, files: Union[List[str], List[Tuple[str, int]]]) -> None:
            if files:
                lines.append(title + ':')
                lines.extend(self._format_file_list(files))

        add_file_list("Files copied", self.files_copied)
        add_file_list("Files not copied due to errors", self.files_not_copied_due_to_errors)
        add_file_list("Files not copied due to size limit", self.files_too_large)
        add_file_list("Files not found", self.files_not_found)

        if self.total_time_sec > 0:
            lines.append(f"Total time: {self.total_time_sec:.2f} sec")
        if self.total_wait_time_sec > 0:
            lines.append(f"Total wait time: {self.total_wait_time_sec:.2f} sec")
        if self.num_copy_errors > 0:
            lines.append(f"Number of errors: {self.num_copy_errors}")
        if self.exception_traceback:
            lines.append("Exception traceback:")
            lines.append(self.exception_traceback)

        return '\n'.join(lines)

    def has_errors(self) -> bool:
        # We do not look at num_copy_errors here because those errors might have happened during
        # attempts that were eventually retried and succeeded.
        return (len(self.files_not_copied_due_to_errors) > 0 or
                len(self.files_too_large) > 0 or
                len(self.files_not_found) > 0)


def copy_artifacts_to_host(
        artifact_paths: List[str],
        dest_host: Optional[str],
        method: UploadMethod,
        max_file_size: Optional[int] = None,
        path_transformer: Callable[[str], str] = lambda s: s,
        initial_retry_delay_sec: float = DEFAULT_UPLOAD_RETRY_INITIAL_DELAY_SEC,
        delay_multiplier: float = DEFAULT_UPLOAD_RETRY_DELAY_MULTIPLIER,
        max_attempts_per_file: int = DEFAULT_MAX_ATTEMPTS_PER_FILE) -> FileTransferResult:
    """
    Copy artifacts back to the destination host via a cp command (e.g. using NFS) or scp (over SSH).

    :param artifact_paths: List of paths to the artifacts to copy. These are converted to absolute
        paths if necessary.
    :param dest_host: Host to copy the artifacts to, if using the SSH method. Regardless of the
        method, if this is specified and is the same as the current host name, we skip the copying
        on macOS.
    :param method: Method to use for copying the artifacts, SSH or CP.
    :param max_file_size: Maximum size of a file to copy, in bytes. Files larger than this will be
        skipped and added to the result's files_too_large list. If this is not specified, no size
        restriction is applied.
    :param path_transformer: Function to transform the path of each artifact before copying it.
        This is only used for the CP copying method.
    :param initial_retry_delay_sec: Initial delay between retries of a single file, in seconds.
    :param delay_multiplier: Multiplier for the delay between retries of a single file. E.g. if
        this is set to 2, the delay will double after each retry.
    :param max_attempts_per_file: Maximum number of attempts to copy a single file.
    :return: Result of the copying.
    """

    artifact_paths = [os.path.abspath(artifact_path) for artifact_path in artifact_paths]
    result = FileTransferResult()

    start_time_sec = time.time()

    if dest_host is not None and is_macos() and socket.gethostname() == dest_host:
        logging.info("Files already local to build host. Skipping artifact copy.")
        return result

    if method == UploadMethod.SSH and dest_host is None:
        raise ValueError("dest_host must be specified when using the SSH method")

    current_delay_sec = initial_retry_delay_sec

    existing_dirs: Set[str] = set()

    def ensure_dir_exists(dir_path: str) -> None:
        if dir_path in existing_dirs or os.path.isdir(dir_path):
            return
        file_util.mkdir_p(dir_path)
        existing_dirs.add(dir_path)

    for artifact_path in sorted(artifact_paths):
        if not os.path.exists(artifact_path):
            logging.warning("Artifact file does not exist: '%s'", artifact_path)
            result.files_not_found.append(artifact_path)
            result.num_copy_errors += 1
            continue

        artifact_size = os.path.getsize(artifact_path)
        if max_file_size is not None and artifact_size > max_file_size:
            logging.warning(
                "Artifact file {} of size {} bytes exceeds max limit of {} bytes".format(
                    artifact_path, os.path.getsize(artifact_path), max_file_size)
            )
            result.files_too_large.append((artifact_path, artifact_size))
            continue

        attempt_index = 1
        if method == UploadMethod.SSH:
            dest_dir = os.path.dirname(artifact_path)
        elif method == UploadMethod.CP:
            dest_path = path_transformer(artifact_path)
            dest_dir = os.path.dirname(dest_path)
        else:
            raise ValueError(f"Unknown upload method: {method}")

        log_msg_prefix = f"Copying {artifact_path} to "

        while True:
            log_msg = log_msg_prefix
            if method == UploadMethod.SSH:
                log_msg += f"{dest_host}:{dest_dir} using scp"
            else:
                log_msg += f"directory {dest_dir} using cp"
            log_msg += f" (attempt {attempt_index} out of {max_attempts_per_file} for the file)"
            logging.info(log_msg)

            try:
                if method == UploadMethod.SSH:
                    assert dest_host is not None
                    subprocess.check_call(['ssh', dest_host, 'mkdir', '-p', dest_dir])
                    subprocess.check_call(['scp', artifact_path, f'{dest_host}:{dest_dir}/'])
                elif method == UploadMethod.CP:
                    ensure_dir_exists(dest_dir)
                    subprocess.check_call(['cp', '-f', artifact_path, dest_path])
                else:
                    raise ValueError(f"Unknown upload method: {method}")

                result.files_copied.append((artifact_path, artifact_size))
                # Successfully copied this file.
                break

            except Exception as ex:
                result.num_copy_errors += 1

                error_msg = f"Error copying {artifact_path} to {dest_dir} at attempt " \
                            f"{attempt_index} out of {max_attempts_per_file}"
                if attempt_index < max_attempts_per_file:
                    error_msg += " (will retry after waiting %.2f seconds)" % current_delay_sec
                else:
                    error_msg += " (no more retries)"
                logging.exception(error_msg)

                if attempt_index < max_attempts_per_file:
                    result.total_wait_time_sec += current_delay_sec
                    time.sleep(current_delay_sec)
                    current_delay_sec *= delay_multiplier
                    attempt_index += 1
                else:
                    result.files_not_copied_due_to_errors.append((artifact_path, artifact_size))
                    break

    result.total_time_sec = time.time() - start_time_sec
    logging.info("Copied %d artifacts in %.2f seconds, not copied: %d (too large: %d)",
                 result.num_files_copied(),
                 result.total_time_sec,
                 result.num_files_not_copied(),
                 len(result.files_too_large))

    return result
