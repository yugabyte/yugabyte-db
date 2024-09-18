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

# Manages header-only third-party dependencies in src/inline-thirdparty. These dependencies are
# copied from the relevant subdirectories of upstream repositories and typically represent only a
# small portion of the upstream repository.

import logging
import os
import shutil
import subprocess
import tempfile

from pathlib import Path
from typing import Optional, List, Set
from dataclasses import dataclass

import ruamel.yaml

from yugabyte import common_util, file_util, git_util


INLINE_THIRDPARTY_CONFIG_REL_PATH = 'build-support/inline_thirdparty.yml'
INLINE_THIRDPARTY_CONFIG_PATH = os.path.join(
    common_util.YB_SRC_ROOT, INLINE_THIRDPARTY_CONFIG_REL_PATH)
INLINE_THIRDPARTY_SRC_DIR = os.path.join(common_util.YB_SRC_ROOT, 'src', 'inline-thirdparty')

FILE_EXTENSIONS_SUPPORTING_CPP_COMMENTS = ('.c', '.cc', '.cpp', '.h', '.hpp', '.modulemap')


ruamel_yaml_object = ruamel.yaml.YAML()


@dataclass
class InlineDependency:
    name: str
    git_url: str
    src_dir: str
    dest_dir: str
    tag: Optional[str] = None
    commit: Optional[str] = None

    def validate_tag_or_commit_choice(self) -> None:
        if self.tag and self.commit:
            raise ValueError(f"Only one of tag or commit can be specified: {self}")
        if not self.tag and not self.commit:
            raise ValueError(f"One of tag or commit must be specified: {self}")

    @property
    def tag_or_commit_description(self) -> str:
        self.validate_tag_or_commit_choice()
        if self.tag:
            return f"tag {self.tag}"
        if self.commit:
            return f"commit {self.commit}"
        raise ValueError(f"Should not happen: {self}")

    @property
    def tag_or_commit(self) -> str:
        self.validate_tag_or_commit_choice()
        result = self.tag or self.commit
        assert result is not None
        return result

    def get_github_commits_url(self, resolved_commit: str) -> str:
        return self.git_url + '/commits/' + resolved_commit


@dataclass
class DependenciesConfig:
    dependencies: List[InlineDependency]


def read_yaml(file_path: str) -> DependenciesConfig:
    """Reads the YAML file and maps it to DependenciesConfig."""
    with open(file_path) as file:
        data = ruamel_yaml_object.load(file)
        dependencies = [InlineDependency(**dep) for dep in data['dependencies']]
        return DependenciesConfig(dependencies=dependencies)


def get_latest_commit_explanation(
        dep: InlineDependency,
        latest_commit_in_subdir: str,
        cpp_comment: bool = False) -> str:
    return (
        f"Latest commit in the {dep.src_dir} subdirectory of the {dep.name} repository:\n" +
        ("// " if cpp_comment else "") +
        latest_commit_in_subdir
    )


def add_comment_to_file(
        file_path: str,
        dep: InlineDependency,
        latest_commit_in_subdir: str) -> None:
    """Adds a comment to the include file indicating what version of the dependcy is being used."""
    if not file_path.endswith(FILE_EXTENSIONS_SUPPORTING_CPP_COMMENTS):
        logging.info("Cannot add comment to file %s", file_path)
        return
    if os.path.islink(file_path):
        logging.info("Cannot add comment to symlink %s", file_path)
        return

    content = file_util.read_file(file_path)
    comment = "\n".join([
        f"// This file is part of the {dep.name} inline third-party dependency of YugabyteDB.",
        f"// Git repo: {dep.git_url}",
        f"// Git tag: {dep.tag}" if dep.tag else f"// Git commit: {dep.commit}",
        f"// {get_latest_commit_explanation(dep, latest_commit_in_subdir, cpp_comment=True)}",
        "//",
        "// See also src/inline-thirdparty/README.md.",
    ])
    file_util.write_file(comment + '\n\n' + content, file_path)


def validate_dir(dep: InlineDependency, dir_type: str) -> None:
    dir_value = getattr(dep, dir_type)
    if not dir_value:
        raise ValueError(f"{dir_type} is required for {dep.name}")

    if os.path.isabs(dir_value):
        raise ValueError(f"{dir_type} must be a relative path for {dep.name}")

    if not dep.git_url.startswith('https://github.com/'):
        raise ValueError(f"git_url must be a GitHub URL for {dep.name}")


def validate_config(config: DependenciesConfig) -> None:
    """Validates the config."""
    names_seen: Set[str] = set()
    for dep in config.dependencies:
        if dep.name in names_seen:
            raise ValueError(f"Duplicate name {dep.name}")
        names_seen.add(dep.name)

        if not dep.git_url:
            raise ValueError(f"git_url is required for {dep.name}")

        validate_dir(dep, "src_dir")
        validate_dir(dep, "dest_dir")
        if dep.dest_dir != dep.name and not dep.dest_dir.startswith(dep.name + '/'):
            raise ValueError(
                f"dest_str must be the same as dependency name or have the dependency name as "
                f"its first relative path component for {dep.name}: {dep}")
        dep.validate_tag_or_commit_choice()


def clone_and_copy_subtrees(dependencies: List[InlineDependency]) -> None:
    """Clones repositories into a temporary directory and copies the subtrees."""
    src_root = Path(INLINE_THIRDPARTY_SRC_DIR)

    with tempfile.TemporaryDirectory() as temp_dir:
        temp_path = Path(temp_dir)

        for dep in dependencies:
            repo_dir = temp_path / dep.name
            logging.info(f"Cloning {dep.name} into {repo_dir}")

            # Clone the repository into the temp directory
            subprocess.check_call(['git', 'clone', dep.git_url, str(repo_dir)])

            # Checkout the specified tag or commit
            subprocess.check_call(['git', 'checkout', dep.tag_or_commit], cwd=repo_dir)

            resolved_commit = git_util.get_latest_commit_in_subdir(str(repo_dir), subdir='.')
            if dep.commit and resolved_commit != dep.commit:
                raise ValueError(
                    f"Expected resolved commit {resolved_commit} to match configured commit "
                    f"{dep.commit} for dependency {dep.name}")

            # Define source and destination directories
            src_subtree = repo_dir / dep.src_dir
            dest_subtree = src_root / dep.dest_dir

            logging.info("Copying subtree from {} to {}".format(src_subtree, dest_subtree))

            # Ensure the destination directory exists
            dest_subtree.parent.mkdir(parents=True, exist_ok=True)

            # Remove the current content in the destination directory. We remove the entire
            # top-level directory under inline-thirdparty, even though dest_dir could contain
            # multiple path components.
            subtree_to_remove = src_root / Path(dep.dest_dir).parts[0]
            if subtree_to_remove.exists():
                logging.info(f"Deleting existing directory {subtree_to_remove}")
                shutil.rmtree(subtree_to_remove)

            # Copy the subtree to the destination directory
            shutil.copytree(src_subtree, dest_subtree)

            latest_commit_in_subdir = git_util.get_latest_commit_in_subdir(
                str(repo_dir), dep.src_dir)
            for root, dirs_unused, files in os.walk(dest_subtree):
                for file in files:
                    file_path = os.path.join(root, file)
                    add_comment_to_file(file_path, dep, latest_commit_in_subdir)

            # Commit the changes in the current repository
            make_commit(dep, latest_commit_in_subdir, resolved_commit)


def make_commit(
        dep: InlineDependency, latest_commit_in_subdir: str, resolved_commit: str) -> None:
    """Creates a descriptive commit in the main YugabyteDB repo for the updated dependency."""
    git_util.validate_git_commit(latest_commit_in_subdir)
    git_util.validate_git_commit(resolved_commit)

    if git_util.is_git_clean(common_util.YB_SRC_ROOT):
        logging.info(f"No changes were made to the {dep.name} dependency, nothing to commit.")
        return

    commit_message_lines = [
        "Automatic commit by thirdparty_tool: " +
        f"update {dep.name} to {dep.tag_or_commit_description}.",
        "",
        f"Used commit of the {dep.name} repository: {dep.get_github_commits_url(resolved_commit)}",
    ]
    if latest_commit_in_subdir != resolved_commit:
        commit_message_lines.extend([
            "",
            get_latest_commit_explanation(dep, latest_commit_in_subdir)
        ])

    commit_message = "\n".join(commit_message_lines)
    subprocess.check_call(['git', 'add', '.'], cwd=INLINE_THIRDPARTY_SRC_DIR)
    subprocess.check_call(['git', 'commit', '-m', commit_message], cwd=INLINE_THIRDPARTY_SRC_DIR)
    logging.info(f"Created an automatic commit for {dep.name}")


def sync_inline_thirdparty() -> None:
    config = read_yaml(INLINE_THIRDPARTY_CONFIG_PATH)
    validate_config(config)
    if not git_util.is_git_clean(common_util.YB_SRC_ROOT):
        raise RuntimeError(f"Local changes exist, cannot update inline third-party dependencies.")
    clone_and_copy_subtrees(config.dependencies)
