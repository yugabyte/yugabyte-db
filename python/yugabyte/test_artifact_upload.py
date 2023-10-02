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

import pathlib
import pytest
import os

from typing import List, Tuple, Set, Union, Dict, Any, cast
from yugabyte import artifact_upload, file_util


def to_path_set(path_list: Union[List[str], List[Tuple[str, int]]]) -> Set[str]:
    if not path_list:
        return set()
    if isinstance(path_list[0], str):
        return set(cast(List[str], path_list))
    return set([t[0] for t in path_list])


def test_copy_artifacts_locally(tmp_path: pathlib.Path) -> None:
    src_dir = tmp_path / 'src_dir'
    dest_dir = tmp_path / 'dest_dir'
    file_util.mkdir_p(src_dir)
    file1_path = src_dir / 'file1.txt'
    file_util.write_file('file1 contents', file1_path)

    file2_path = src_dir / 'file2.txt'
    file_util.write_file('file2 contents', file2_path)

    file3_path_nonexistent = src_dir / 'file3_nonexistent.txt'

    file4_path_large = src_dir / 'file4_large.txt'
    file_util.write_file('file4 contents: ' + '!' * 100, file4_path_large)

    def path_transformer(src_path: str) -> str:
        return os.path.join(dest_dir, os.path.relpath(src_path, str(src_dir)))

    copy_args: Dict[str, Any] = dict(
        artifact_paths=[
            str(file1_path),
            str(file2_path),
            str(file3_path_nonexistent),
        ],
        dest_host=None,
        method=artifact_upload.UploadMethod.CP,
        path_transformer=path_transformer,
        max_file_size=None)

    result = artifact_upload.copy_artifacts_to_host(**copy_args)  # type: ignore
    assert to_path_set(result.files_copied) == {
        str(file1_path), str(file2_path)
    }
    assert to_path_set(result.files_not_found) == set([str(file3_path_nonexistent)])
    assert len(result.files_too_large) == 0
    assert len(result.files_not_copied_due_to_errors) == 0

    copy_args['max_file_size'] = 50
    copy_args['artifact_paths'].append(str(file4_path_large))
    result = artifact_upload.copy_artifacts_to_host(**copy_args)  # type: ignore

    def check_result_with_large_file() -> None:
        assert to_path_set(result.files_copied) == {str(file1_path), str(file2_path)}
        assert to_path_set(result.files_not_found) == {str(file3_path_nonexistent)}
        assert to_path_set(result.files_too_large) == {str(file4_path_large)}
        assert len(result.files_not_copied_due_to_errors) == 0
    check_result_with_large_file()

    # This could be run locally if SSH from localhost to localhost is enabled.
    if os.getenv('YB_TEST_ARTIFACT_UPLOAD_SSH_METHOD') == '1':
        copy_args.update(
            method=artifact_upload.UploadMethod.SSH,
            dest_host='localhost')
        result = artifact_upload.copy_artifacts_to_host(**copy_args)  # type: ignore
        check_result_with_large_file()
