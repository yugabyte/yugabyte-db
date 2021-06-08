#!/usr/bin/env python

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

"""
This is a command-line tool that allows to get the download URL for a prebuilt third-party
dependencies archive for a particular configuration, as well as to update these URLs based
on the recent releases in the https://github.com/yugabyte/yugabyte-db-thirdparty repository.
"""

import sys
import re
import os
import logging
import argparse

from github import Github
from github.GitRelease import GitRelease

from typing import Dict, List, Any, Optional
from datetime import datetime

from yb.common_util import (
    init_env, YB_SRC_ROOT, read_file, load_yaml_file, write_yaml_file, to_yaml_str)
from yb.os_detection import get_short_os_name


THIRDPARTY_ARCHIVES_REL_PATH = os.path.join('build-support', 'thirdparty_archives.yml')

NUM_TOP_COMMITS = 10

DOWNLOAD_URL_PREFIX = 'https://github.com/yugabyte/yugabyte-db-thirdparty/releases/download/'
TAG_RE = re.compile(
    r'^v(?:(?P<branch_name>[0-9.]+)-)?'
    r'(?P<timestamp>[0-9]+)-'
    r'(?P<sha_prefix>[0-9a-f]+)'
    r'(?:-(?P<architecture>x86_64|arm64))?'
    r'(?:-(?P<os>(?:ubuntu|centos|macos|alpine)[a-z0-9.]*))'
    r'(?:-(?P<is_linuxbrew>linuxbrew))?'
    # "devtoolset" really means just "gcc" here. We should replace it with "gcc" in release names.
    r'(?:-(?P<compiler_type>(?:gcc|clang|devtoolset-?)[a-z0-9.]+))?'
    r'$')

# We will store the SHA1 to be used for the local third-party checkout under this key.
SHA_FOR_LOCAL_CHECKOUT_KEY = 'sha_for_local_checkout'

UBUNTU_OS_TYPE_RE = re.compile(r'^(ubuntu)([0-9]{2})([0-9]{2})$')


def get_archive_name_from_tag(tag: str) -> str:
    return f'yugabyte-db-thirdparty-{tag}.tar.gz'


def adjust_os_type(os_type: str) -> str:
    match = UBUNTU_OS_TYPE_RE.match(os_type)
    if match:
        # Convert e.g. ubuntu2004 -> ubuntu20.04 for clarity.
        return f'{match.group(1)}{match.group(2)}.{match.group(3)}'
    return os_type


class YBDependenciesRelease:

    FIELDS_TO_PERSIST = ['os_type', 'compiler_type', 'tag', 'sha']

    github_release: GitRelease
    sha: str

    timestamp: str
    url: str
    compiler_type: str
    os_type: str
    tag: str
    branch_name: Optional[str]

    def __init__(self, github_release: GitRelease) -> None:
        self.github_release = github_release
        self.sha = self.github_release.target_commitish

        tag = self.github_release.tag_name
        tag_match = TAG_RE.match(tag)
        if not tag_match:
            raise ValueError(f"Could not parse tag: {tag}, does not match regex: {TAG_RE}")

        group_dict = tag_match.groupdict()

        sha_prefix = tag_match.group('sha_prefix')
        if not self.sha.startswith(sha_prefix):
            raise ValueError(
                f"SHA prefix {sha_prefix} extracted from tag {tag} is not a prefix of the "
                f"SHA corresponding to the release/tag: {self.sha}.")

        self.timestamp = group_dict['timestamp']
        self.os_type = adjust_os_type(group_dict['os'])
        self.is_linuxbrew = bool(group_dict.get('is_linuxbrew'))

        compiler_type = group_dict.get('compiler_type')
        if compiler_type is None and self.os_type == 'macos':
            compiler_type = 'clang'
        if compiler_type is None and self.is_linuxbrew:
            compiler_type = 'gcc'

        if compiler_type is None:
            raise ValueError(
                f"Could not determine compiler type from tag {tag}. Matches: {group_dict}.")
        compiler_type = compiler_type.strip('-')
        self.tag = tag
        self.compiler_type = compiler_type

        branch_name = group_dict.get('branch_name')
        if branch_name is not None:
            branch_name = branch_name.rstrip('-')
        self.branch_name = branch_name

    def validate_url(self) -> None:
        asset_urls = [asset.browser_download_url for asset in self.github_release.get_assets()]

        assert(len(asset_urls) == 2)
        non_checksum_urls = [url for url in asset_urls if not url.endswith('.sha256')]
        assert(len(non_checksum_urls) == 1)
        self.url = non_checksum_urls[0]
        if not self.url.startswith(DOWNLOAD_URL_PREFIX):
            raise ValueError(
                f"Expected archive download URL to start with {DOWNLOAD_URL_PREFIX}, found "
                f"{self.url}")

        url_suffix = self.url[len(DOWNLOAD_URL_PREFIX):]
        url_suffix_components = url_suffix.split('/')
        assert(len(url_suffix_components) == 2)

        archive_basename = url_suffix_components[1]
        expected_basename = get_archive_name_from_tag(self.tag)
        if archive_basename != expected_basename:
            raise ValueError(
                f"Expected archive name based on tag: {expected_basename}, "
                f"actual name: {archive_basename}, url: {self.url}")

    def as_dict(self) -> Dict[str, str]:
        return {k: getattr(self, k) for k in self.FIELDS_TO_PERSIST}

    def get_sort_key(self) -> List[str]:
        return [getattr(self, k) for k in self.FIELDS_TO_PERSIST]

    def is_consistent_with_yb_version(self, yb_version: str) -> bool:
        return (self.branch_name is None or
                yb_version.startswith((self.branch_name + '.', self.branch_name + '-')))

    def __str__(self) -> str:
        return str(self.as_dict())


class ReleaseGroup:
    sha: str
    releases: List[YBDependenciesRelease]
    creation_timestamps: List[datetime]

    def __init__(self, sha: str) -> None:
        self.sha = sha
        self.releases = []
        self.creation_timestamps = []

    def add_release(self, release: YBDependenciesRelease) -> None:
        if release.sha != self.sha:
            raise ValueError(
                f"Adding a release with wrong SHA. Expected: {self.sha}, got: "
                f"{release.sha}.")
        self.releases.append(release)
        self.creation_timestamps.append(release.github_release.created_at)

    def get_max_creation_timestamp(self) -> datetime:
        return max(self.creation_timestamps)

    def get_min_creation_timestamp(self) -> datetime:
        return min(self.creation_timestamps)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--github-token-file',
        help='Read GitHub token from this file. Authenticated requests have a higher rate limit. '
             'If this is not specified, we will still use the GITHUB_TOKEN environment '
             'variable.')
    parser.add_argument(
        '--update', '-u', action='store_true',
        help=f'Update the third-party archive metadata in in {THIRDPARTY_ARCHIVES_REL_PATH}.')
    parser.add_argument(
        '--get-sha1',
        action='store_true',
        help='Show the Git SHA1 of the commit to use in the yugabyte-db-thirdparty repo '
             'in case we are building the third-party dependencies from scratch.')
    parser.add_argument(
        '--save-download-url-to-file',
        help='Determine the third-party archive download URL for the combination of criteria, '
             'including the compiler type, and write it to the file specified by this argument.')
    parser.add_argument(
        '--compiler-type',
        help='Compiler type, to help us decide which third-party archive to choose. '
             'The default value is determined by the YB_COMPILER_TYPE environment variable.',
        default=os.getenv('YB_COMPILER_TYPE'))
    parser.add_argument(
        '--os-type',
        help='Operating system type, to help us decide which third-party archive to choose. '
             'The default value is determined automatically based on the current OS.')
    parser.add_argument(
        '--verbose',
        help='Verbose debug information')

    if len(sys.argv) == 1:
        parser.print_help(sys.stderr)
        sys.exit(1)

    return parser.parse_args()


def get_archive_metadata_file_path() -> str:
    return os.path.join(YB_SRC_ROOT, THIRDPARTY_ARCHIVES_REL_PATH)


def get_github_token(token_file_path: Optional[str]) -> Optional[str]:
    github_token: Optional[str]
    if token_file_path:
        github_token = read_file(token_file_path).strip()
    else:
        github_token = os.getenv('GITHUB_TOKEN')
    if github_token is None:
        return github_token

    if len(github_token) != 40:
        raise ValueError(f"Invalid GitHub token length: {len(github_token)}, expected 40.")
    return github_token


def update_archive_metadata_file(github_token_file: Optional[str]) -> None:
    yb_version = read_file(os.path.join(YB_SRC_ROOT, 'version.txt')).strip()

    archive_metadata_path = get_archive_metadata_file_path()
    logging.info(f"Updating third-party archive metadata file in {archive_metadata_path}")

    github_client = Github(get_github_token(github_token_file))
    repo = github_client.get_repo('yugabyte/yugabyte-db-thirdparty')

    releases_by_commit: Dict[str, ReleaseGroup] = {}

    num_skipped_old_tag_format = 0
    num_skipped_wrong_branch = 0
    num_releases_found = 0
    for release in repo.get_releases():
        sha: str = release.target_commitish
        assert(isinstance(sha, str))
        tag_name = release.tag_name
        if len(tag_name.split('-')) <= 2:
            logging.debug(f"Skipping release tag: {tag_name} (old format, too few components)")
            num_skipped_old_tag_format += 1
            continue

        yb_dep_release = YBDependenciesRelease(release)
        if not yb_dep_release.is_consistent_with_yb_version(yb_version):
            logging.debug(f"Skipping release tag: {tag_name} (does not match version {yb_version}")
            num_skipped_wrong_branch += 1
            continue

        if sha not in releases_by_commit:
            releases_by_commit[sha] = ReleaseGroup(sha)

        num_releases_found += 1
        logging.info(f"Found release: {yb_dep_release}")
        releases_by_commit[sha].add_release(yb_dep_release)

    if num_skipped_old_tag_format > 0:
        logging.info(f"Skipped {num_skipped_old_tag_format} releases due to old tag format")
    if num_skipped_wrong_branch > 0:
        logging.info(f"Skipped {num_skipped_wrong_branch} releases due to branch mismatch")
    logging.info(
        f"Found {num_releases_found} releases for {len(releases_by_commit)} different commits")
    latest_group_by_max = max(
        releases_by_commit.values(), key=ReleaseGroup.get_max_creation_timestamp)
    latest_group_by_min = max(
        releases_by_commit.values(), key=ReleaseGroup.get_min_creation_timestamp)
    if latest_group_by_max is not latest_group_by_min:
        raise ValueError(
            "Overlapping releases for different commits. No good way to identify latest release: "
            f"e.g. {latest_group_by_max.sha} and {latest_group_by_min.sha}.")

    latest_group = latest_group_by_max

    sha = latest_group.sha
    logging.info(
        f"Latest released yugabyte-db-thirdparty commit: f{sha}. "
        f"Released at: {latest_group.get_max_creation_timestamp()}.")

    new_metadata: Dict[str, Any] = {
        SHA_FOR_LOCAL_CHECKOUT_KEY: sha,
        'archives': []
    }
    releases_for_one_commit = latest_group.releases
    for yb_thirdparty_release in releases_for_one_commit:
        yb_thirdparty_release.validate_url()

    releases_for_one_commit.sort(key=YBDependenciesRelease.get_sort_key)

    for yb_thirdparty_release in releases_for_one_commit:
        new_metadata['archives'].append(yb_thirdparty_release.as_dict())

    write_yaml_file(new_metadata, archive_metadata_path)
    logging.info(
        f"Wrote information for {len(releases_for_one_commit)} pre-built yugabyte-db-thirdparty "
        f"archives to {archive_metadata_path}.")


def load_metadata() -> Dict[str, Any]:
    return load_yaml_file(get_archive_metadata_file_path())


def get_download_url(
        metadata: Dict[str, Any],
        compiler_type: str,
        os_type: Optional[str]) -> str:
    if not os_type:
        os_type = get_short_os_name()

    candidates: List[Any] = []
    available_archives = metadata['archives']
    for archive in available_archives:
        if archive['os_type'] == os_type and archive['compiler_type'] == compiler_type:
            candidates.append(archive)

    if len(candidates) == 1:
        tag = candidates[0]['tag']
        return f'{DOWNLOAD_URL_PREFIX}{tag}/{get_archive_name_from_tag(tag)}'

    if not candidates:
        if os_type == 'centos7' and compiler_type == 'gcc':
            logging.info(
                "Assuming that the compiler type of 'gcc' means 'gcc5'. "
                "This will change when we stop using Linuxbrew and update the compiler.")
            return get_download_url(metadata, 'gcc5', os_type)

        logging.info(f"Available release archives:\n{to_yaml_str(available_archives)}")
        raise ValueError(
            "Could not find a third-party release archive to download for OS type "
            f"'{os_type}' and compiler type '{compiler_type}'. "
            "Please see the list of available thirdparty archives above.")

    raise ValueError(
        f"Found too many third-party release archives to download for OS type "
        f"{os_type} and compiler type matching {compiler_type}: {candidates}.")


def main() -> None:
    args = parse_args()
    init_env(verbose=args.verbose)
    if args.update:
        update_archive_metadata_file(args.github_token_file)
        return
    metadata = load_metadata()
    if args.get_sha1:
        print(metadata[SHA_FOR_LOCAL_CHECKOUT_KEY])
        return

    if args.save_download_url_to_file:
        if not args.compiler_type:
            raise ValueError("Compiler type not specified")
        url = get_download_url(
            metadata=metadata,
            compiler_type=args.compiler_type,
            os_type=args.os_type)
        if url is None:
            raise RuntimeError("Could not determine download URL")
        logging.info(f"Download URL for the third-party dependencies: {url}")
        output_file_dir = os.path.dirname(os.path.abspath(args.save_download_url_to_file))
        os.makedirs(output_file_dir, exist_ok=True)
        with open(args.save_download_url_to_file, 'w') as output_file:
            output_file.write(url)


if __name__ == '__main__':
    main()
