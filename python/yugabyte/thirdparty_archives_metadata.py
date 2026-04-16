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

# Tools for manipulating the thirdparty_archives.yml file.

import logging
import os
import pprint
import re
import time

from collections import defaultdict
from io import StringIO
from re import Pattern
from typing import Set, Dict, Any, Optional, List, Union, DefaultDict, Tuple

import ruamel.yaml

from github import Github, GithubException
from github.Artifact import Artifact

from yugabyte.thirdparty_releases import (
    RELEASES_DOWNLOAD_URL,
    RUN_ARTIFACT_DOWNLOAD_URL,
    get_archive_name_from_tag,
    ThirdPartyReleaseBase,
)
from yugabyte import common_util
from yugabyte.common_util import (
    YB_SRC_ROOT,
    load_yaml_file,
)
from yugabyte.file_util import read_file
from yugabyte.git_util import get_github_token, is_valid_git_sha
from yugabyte.thirdparty_releases import (
    ReleaseGroup,
    GitHubThirdPartyArtifact,
    GitHubThirdPartyRelease,
    SkipThirdPartyReleaseException,
)


ruamel_yaml_object = ruamel.yaml.YAML()


# A plain Python data structure nested type for the thirdparty_archives.yml file structure.
ThirdPartyArchivesYAML = Dict[str, Union[str, List[Dict[str, str]]]]


THIRDPARTY_ARCHIVES_REL_PATH = os.path.join('build-support', 'thirdparty_archives.yml')
MANUAL_THIRDPARTY_ARCHIVES_REL_PATH = os.path.join(
    'build-support', 'thirdparty_archives_manual.yml')

# Skip these problematic tags.
BROKEN_TAGS = set(['v20210907234210-47a70bc7dc-centos7-x86_64-linuxbrew-gcc5'])

# We will store the SHA1 to be used for the local third-party checkout, as well as to use by default
# for individual archives unless it is overridden, under this top-level key in the third-party
# archive metaadata YAML files. Furthermore, for each third-party release, we will store the SHA
# of the commit that corresponds to the release tag under the same key, unless it is different from
# the default value stored at the top level.
SHA_KEY = 'sha'

# The top-level key under which we store metadata for third-party archives.
ARCHIVES_KEY = 'archives'

CHECKSUM_EXTENSION = '.sha256'


@ruamel_yaml_object.register_class
class MetadataItem(ThirdPartyReleaseBase):
    """
    A metadata item for a third-party download archive loaded from the thirdparty_archives.yml
    file.
    """

    def __init__(self, yaml_data: Dict[str, Any]) -> None:
        processed_field_names: Set[str] = set()
        for field_name in MetadataItem.KEY_FIELDS_WITH_TAG:
            field_value = yaml_data.get(field_name)
            if field_value is None:
                field_value = ThirdPartyReleaseBase.get_default_field_value(field_name)
            setattr(self, field_name, field_value)
            processed_field_names.add(field_name)
        unknown_fields = set(yaml_data.keys() - processed_field_names)
        if unknown_fields:
            raise ValueError(
                "Unknown fields found in third-party metadata YAML file: %s. "
                "Entire item: %s" % (sorted(unknown_fields), pprint.pformat(yaml_data)))

    def url(self) -> str:
        if self.release_artifact_id:
            return RUN_ARTIFACT_DOWNLOAD_URL.format(self.release_artifact_id)
        return RELEASES_DOWNLOAD_URL.format(self.tag, get_archive_name_from_tag(self.tag))

    def checksum_url(self) -> str:
        if self.checksum_artifact_id:
            return RUN_ARTIFACT_DOWNLOAD_URL.format(self.checksum_artifact_id)
        return RELEASES_DOWNLOAD_URL.format(
                self.tag, get_archive_name_from_tag(self.tag) + CHECKSUM_EXTENSION)


def get_archive_metadata_file_path() -> str:
    return os.path.join(YB_SRC_ROOT, THIRDPARTY_ARCHIVES_REL_PATH)


def get_manual_archive_metadata_file_path() -> str:
    return os.path.join(YB_SRC_ROOT, MANUAL_THIRDPARTY_ARCHIVES_REL_PATH)


class MetadataUpdater:
    github_token_file_path: str
    tag_filter_pattern: Optional[Pattern]
    also_use_commits: List[str]
    archive_metadata_path: str
    override_default_sha: Optional[str]

    def __init__(
            self,
            github_token_file_path: str,
            tag_filter_regex_str: Optional[str],
            also_use_commits: List[str],
            override_default_sha: Optional[str]) -> None:
        self.github_token_file_path = github_token_file_path
        if tag_filter_regex_str:
            self.tag_filter_pattern = re.compile(tag_filter_regex_str)
        else:
            self.tag_filter_pattern = None
        self.also_use_commits = also_use_commits
        self.archive_metadata_path = get_archive_metadata_file_path()
        self.override_default_sha = override_default_sha

    def update_archive_metadata_file(self, thirdparty_pr: Optional[int]) -> None:
        if thirdparty_pr:
            self.update_archive_metadata_file_with_pr(thirdparty_pr)
        else:
            self.update_archive_metadata_file_for_release()

    def update_archive_metadata_file_with_pr(self, thirdparty_pr: int) -> None:
        yb_version = read_file(os.path.join(YB_SRC_ROOT, 'version.txt')).strip()

        logging.info(f"Updating third-party archive metadata file in {self.archive_metadata_path} "
                     f"using PR #{thirdparty_pr}")

        github_client = Github(get_github_token(self.github_token_file_path))
        repo = github_client.get_repo('yugabyte/yugabyte-db-thirdparty')

        pr = repo.get_pull(thirdparty_pr)
        *_, last_commit = pr.get_commits()

        logging.info(f'Using commit {last_commit.sha}')
        release_artifacts: Dict[str, Artifact] = {}
        checksum_artifacts: Dict[str, Artifact] = {}
        for run in repo.get_workflow_runs(head_sha=last_commit.sha):
            for artifact in run.get_artifacts():  # type: ignore
                if not artifact.expired:
                    if artifact.name.endswith(CHECKSUM_EXTENSION):
                        artifacts = checksum_artifacts
                        key = artifact.name[:-len(CHECKSUM_EXTENSION)]
                    else:
                        artifacts = release_artifacts
                        key = artifact.name
                    if key in artifacts:
                        logging.info(f'Found duplicate artifact: {artifact.name} ({artifact.id})')
                        if artifact.created_at < artifacts[key].created_at:
                            logging.info(f'Artifact is older, skipping')
                            continue
                        logging.info(f'Artifact is newer, using')
                    else:
                        logging.info(f'Found artifact: {artifact.name} ({artifact.id})')
                    artifacts[key] = artifact

        artifacts_to_use = [
            (release_artifacts[name], checksum_artifacts[name])
            for name in sorted(artifacts.keys())
        ]
        new_metadata: ThirdPartyArchivesYAML = {
            SHA_KEY: last_commit.sha
        }
        archives: List[Dict[str, str]] = []
        for release_artifact, checksum_artifact in artifacts_to_use:
            release = GitHubThirdPartyArtifact(
                release_artifact.name, last_commit.sha, release_artifact.id, checksum_artifact.id)
            release_as_dict = release.as_dict()
            # To reduce the size of diffs when updating third-party archives YAML file.
            del release_as_dict[SHA_KEY]
            archives.append(release_as_dict)
        new_metadata[ARCHIVES_KEY] = archives

        self.write_metadata_file(new_metadata, warn_no_land=True)
        logging.info(
            f"Wrote information for {len(artifacts_to_use)} pre-built "
            f"yugabyte-db-thirdparty archives to {self.archive_metadata_path}.")

    def update_archive_metadata_file_for_release(self) -> None:
        yb_version = read_file(os.path.join(YB_SRC_ROOT, 'version.txt')).strip()

        logging.info(f"Updating third-party archive metadata file in {self.archive_metadata_path}")

        github_client = Github(get_github_token(self.github_token_file_path))
        repo = github_client.get_repo('yugabyte/yugabyte-db-thirdparty')

        releases_by_commit: Dict[str, ReleaseGroup] = {}
        num_skipped_old_tag_format = 0
        num_skipped_wrong_branch = 0
        num_skipped_too_os_specific = 0
        num_releases_found = 0

        releases = []
        get_releases_start_time_sec = time.time()
        try:
            for release in repo.get_releases():
                releases.append(release)
        except GithubException as exc:
            if 'Only the first 1000 results are available.' in str(exc):
                logging.info("Ignoring exception: %s", exc)
            else:
                raise exc
        logging.info("Time spent to iterate all releases: %.1f sec",
                     time.time() - get_releases_start_time_sec)

        for release in releases:
            sha: str = release.target_commitish
            assert isinstance(sha, str)

            if not is_valid_git_sha(sha):
                sha = repo.get_commit(sha).sha

            tag_name = release.tag_name
            if len(tag_name.split('-')) <= 2:
                logging.debug(f"Skipping release tag: {tag_name} (old format, too few components)")
                num_skipped_old_tag_format += 1
                continue
            if self.tag_filter_pattern and not self.tag_filter_pattern.match(tag_name):
                logging.info(f'Skipping tag {tag_name}, does not match the filter')
                continue

            try:
                yb_dep_release = GitHubThirdPartyRelease(release, target_commitish=sha)
            except SkipThirdPartyReleaseException as ex:
                logging.warning("Skipping release: %s", ex)
                continue

            if not yb_dep_release.is_consistent_with_yb_version(yb_version):
                logging.info(
                    f"Skipping release tag: {tag_name} (does not match version {yb_version})")
                num_skipped_wrong_branch += 1
                continue

            if sha not in releases_by_commit:
                releases_by_commit[sha] = ReleaseGroup(sha)

            num_releases_found += 1
            logging.debug(f"Found release: {yb_dep_release}")
            releases_by_commit[sha].add_release(yb_dep_release)

        if num_skipped_old_tag_format > 0:
            logging.info(f"Skipped {num_skipped_old_tag_format} releases due to old tag format")
        if num_skipped_wrong_branch > 0:
            logging.info(f"Skipped {num_skipped_wrong_branch} releases due to branch mismatch")
        if num_skipped_too_os_specific > 0:
            logging.info(f"Skipped {num_skipped_too_os_specific} releases as too OS-specific")
        logging.info(
            f"Found {num_releases_found} releases for {len(releases_by_commit)} different commits")

        latest_group_by_max = max(
            releases_by_commit.values(), key=ReleaseGroup.get_max_creation_timestamp)
        latest_group_by_min = max(
            releases_by_commit.values(), key=ReleaseGroup.get_min_creation_timestamp)
        if latest_group_by_max is not latest_group_by_min:
            raise ValueError(
                "Overlapping releases for different commits. No good way to identify latest "
                "release: e.g. {latest_group_by_max.sha} and {latest_group_by_min.sha}.")

        latest_group: ReleaseGroup = latest_group_by_max

        latest_release_sha = latest_group.sha
        logging.info(
            f"Latest released yugabyte-db-thirdparty commit: {latest_release_sha}. "
            f"Released at: {latest_group.get_max_creation_timestamp()}.")

        groups_to_use: List[ReleaseGroup] = [latest_group]

        if self.also_use_commits:
            for extra_commit in self.also_use_commits:
                logging.info(f"Additional manually specified commit to use: {extra_commit}")
                if extra_commit == latest_release_sha:
                    logging.info(
                        f"(already matches the latest commit {latest_release_sha}, skipping.)")
                    continue
                if extra_commit not in releases_by_commit:
                    raise ValueError(
                        f"No releases found for user-specified commit {extra_commit}. "
                        "Please check if there is an error.")
                groups_to_use.append(releases_by_commit[extra_commit])

        releases_to_use: List[GitHubThirdPartyRelease] = [
            rel for release_group in groups_to_use
            for rel in release_group.releases
            if rel.tag not in BROKEN_TAGS
        ]

        default_sha = self.override_default_sha or latest_release_sha
        archives: List[Dict[str, str]] = []
        new_metadata: ThirdPartyArchivesYAML = {
            SHA_KEY: default_sha
        }

        releases_by_key_without_tag: DefaultDict[Tuple[str, ...], List[GitHubThirdPartyRelease]] = \
            defaultdict(list)

        num_valid_releases = 0
        num_invalid_releases = 0
        for yb_thirdparty_release in releases_to_use:
            if yb_thirdparty_release.validate_url():
                num_valid_releases += 1
                releases_by_key_without_tag[
                    yb_thirdparty_release.get_sort_key(include_tag=False)
                ].append(yb_thirdparty_release)
            else:
                num_invalid_releases += 1
        logging.info(
            f"Valid releases found: {num_valid_releases}, invalid releases: {num_invalid_releases}")

        filtered_releases_to_use = []
        for key_without_tag, releases_for_key in releases_by_key_without_tag.items():
            if len(releases_for_key) > 1:
                picked_release = max(releases_for_key, key=lambda r: r.tag)
                logging.info(
                    "Multiple releases found for the same key (excluding the tag). "
                    "Using the latest one: %s\n"
                    "Key: %s.\nReleases:\n  %s" % (
                        picked_release,
                        key_without_tag,
                        '\n  '.join([str(r) for r in releases_for_key])))
                filtered_releases_to_use.append(picked_release)
            else:
                filtered_releases_to_use.append(releases_for_key[0])

        filtered_releases_to_use.sort(key=GitHubThirdPartyRelease.get_sort_key)

        for yb_thirdparty_release in filtered_releases_to_use:
            release_as_dict = yb_thirdparty_release.as_dict()
            if release_as_dict[SHA_KEY] == default_sha:
                # To reduce the size of diffs when updating third-party archives YAML file.
                del release_as_dict[SHA_KEY]
            archives.append(release_as_dict)
        new_metadata[ARCHIVES_KEY] = archives

        self.write_metadata_file(new_metadata)
        logging.info(
            f"Wrote information for {len(filtered_releases_to_use)} pre-built "
            f"yugabyte-db-thirdparty archives to {self.archive_metadata_path}.")

    def write_metadata_file(
            self,
            new_metadata: ThirdPartyArchivesYAML,
            warn_no_land: bool = False) -> None:
        yaml = common_util.get_ruamel_yaml_instance()
        string_stream = StringIO()
        yaml.dump(new_metadata, string_stream)
        yaml_lines = string_stream.getvalue().split('\n')
        new_lines = []
        if warn_no_land:
            new_lines.append(
                '# This file was generated from a thirdparty PR, not a release. DO NOT LAND.')
            new_lines.append('')
        for line in yaml_lines:
            if line.startswith('  -'):
                new_lines.append('')
            new_lines.append(line)
        while new_lines and new_lines[-1].strip() == '':
            new_lines.pop()

        with open(self.archive_metadata_path, 'w') as output_file:
            output_file.write('\n'.join(new_lines) + '\n')


def load_metadata_file(file_path: str) -> ThirdPartyArchivesYAML:
    data = load_yaml_file(file_path)
    default_sha = data.get(SHA_KEY)
    if default_sha is not None:
        for archive in data[ARCHIVES_KEY]:
            if archive.get(SHA_KEY, '').strip() == '':
                archive[SHA_KEY] = default_sha
    if ARCHIVES_KEY not in data:
        data[ARCHIVES_KEY] = []
    unexpected_keys = data.keys() - [ARCHIVES_KEY, SHA_KEY]
    if unexpected_keys:
        raise ValueError(
            f"Found unexpected keys in third-party archive metadata loaded from file {file_path}. "
            f"Details: {pprint.pformat(data)}")
    return data


def load_metadata() -> ThirdPartyArchivesYAML:
    return load_metadata_file(get_archive_metadata_file_path())


def load_manual_metadata() -> ThirdPartyArchivesYAML:
    return load_metadata_file(get_manual_archive_metadata_file_path())
