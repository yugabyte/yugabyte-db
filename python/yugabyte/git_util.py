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

import logging
import os
import re
import subprocess

from typing import Optional

from yugabyte.file_util import read_file


SHA1_RE = re.compile(r'^[0-9a-f]{40}$')


def is_valid_git_sha(commit: str) -> bool:
    return SHA1_RE.match(commit) is not None


def validate_git_commit(commit: str) -> str:
    commit = commit.strip().lower()
    if not is_valid_git_sha(commit):
        raise ValueError(f"Invalid Git commit SHA1: {commit}")
    return commit


def get_github_token(token_file_path: Optional[str]) -> Optional[str]:
    github_token: Optional[str]
    if token_file_path:
        logging.info("Reading GitHub token from %s", token_file_path)
        github_token = read_file(token_file_path).strip()
    else:
        github_token = os.getenv('GITHUB_TOKEN')
    if github_token is None:
        return github_token

    if len(github_token) != 40:
        raise ValueError(f"Invalid GitHub token length: {len(github_token)}, expected 40.")
    return github_token


def is_git_clean(repo_dir: str) -> bool:
    # Check for uncommitted changes (staged or unstaged)
    result = subprocess.run(['git', 'status', '--porcelain'],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            text=True,
                            cwd=repo_dir,
                            check=True)

    # If the result is an empty string, the working directory is clean
    return result.stdout.strip() == ''


def get_latest_commit_in_subdir(repo_dir: str, subdir: str) -> str:
    """
    Get the latest commit that affected a particular subdirectory.
    """
    assert not os.path.isabs(subdir), \
        f"Subdirectory must be a relative path, not an absolute path: {subdir}"
    result = subprocess.run(
        ['git', 'log', '-n', '1', '--pretty=format:%H', '--', subdir],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=repo_dir,
        check=True
    )
    commit_sha = result.stdout.strip()
    if not commit_sha:
        raise ValueError(f"No commits found for subdirectory: {subdir}")
    validate_git_commit(commit_sha)
    return commit_sha
