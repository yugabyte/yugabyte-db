"""
Copyright (c) YugaByte, Inc.

This module provides utilities for generating and publishing release.
"""

import glob
import json
import logging
import os
import platform
import shutil
import sys

from subprocess import call, check_output
from common_util import log_message
from xml.dom import minidom

RELEASE_MANIFEST_NAME = "yb_release_manifest.json"
RELEASE_VERSION_FILE = "version.txt"


class ReleaseUtil(object):
    """ReleaseUtil class is used to package yugabyte release build file with appropriate
    file naming schema.
    """
    def __init__(self, repository, build_type, edition, distribution_path, force):
        self.repo = repository
        self.build_type = build_type
        self.build_path = os.path.join(self.repo, 'build')
        self.edition = edition
        self.distribution_path = distribution_path
        self.force = force
        self.base_version = None
        with open(os.path.join(self.repo, RELEASE_VERSION_FILE)) as v:
            # Remove any build number in the version.txt.
            self.base_version = v.read().split("-")[0]
        assert self.base_version is not None, \
            'Unable to read {0} file'.format(RELEASE_VERSION_FILE)

        with open(os.path.join(self.repo, RELEASE_MANIFEST_NAME)) as f:
            self.release_manifest = json.load(f)
        assert self.release_manifest is not None, \
            'Unable to read {0} file'.format(RELEASE_MANIFEST_NAME)

    def get_release_manifest(self):
        return self.release_manifest

    def get_binary_path(self):
        return self.release_manifest['bin']

    def update_java_version(self):
        # Parse Java project version out of java/pom.xml.
        pom_file = os.path.join(self.repo, 'java', 'pom.xml')
        java_project_version = minidom.parse(pom_file).getElementsByTagName(
            'version')[0].firstChild.nodeValue
        log_message(logging.INFO,
                    "Java project version from pom.xml: {}".format(java_project_version))

        for key, value_list in self.release_manifest.iteritems():
            for i in xrange(len(value_list)):
                new_value = value_list[i].replace('${project.version}', java_project_version)
                if new_value != value_list[i]:
                    log_message(logging.INFO,
                                "Substituting Java project version in '{}' -> '{}'".format(
                                    value_list[i], new_value))
                    value_list[i] = new_value

    def create_distribution(self, distribution_dir):
        """This method would read the release_manifest and traverse through the
        build directory and copy necessary files/symlinks into the distribution_dir
        Args:
            distribution_dir (string): Directory to create the distribution
        """
        for dir_from_manifest in self.release_manifest:
            current_dest_dir = os.path.join(distribution_dir, dir_from_manifest)
            if os.path.exists(current_dest_dir):
                logging.info("Directory '{}' already exists".format(current_dest_dir))
            else:
                os.makedirs(current_dest_dir)

            for elem in self.release_manifest[dir_from_manifest]:
                if not elem.startswith('/'):
                    elem = os.path.join(self.repo, elem)
                files = glob.glob(elem)
                for file_path in files:
                    if os.path.islink(file_path):
                        link_path = os.path.join(current_dest_dir, os.path.basename(file_path))
                        link_target = os.readlink(file_path)
                        log_message(logging.DEBUG,
                                    "Creating symlink {} -> {}".format(link_path, link_target))
                        os.symlink(link_target, link_path)
                    elif os.path.isdir(file_path):
                        current_dest_dir = os.path.join(current_dest_dir,
                                                        os.path.basename(file_path))
                        log_message(logging.DEBUG,
                                    "Copying directory {} to {}".format(file_path,
                                                                        current_dest_dir))
                        shutil.copytree(file_path, current_dest_dir)
                    else:
                        current_dest_dir = os.path.join(distribution_dir, dir_from_manifest)
                        log_message(
                            logging.DEBUG,
                            "Copying file {} to directory {}".format(file_path,
                                                                     current_dest_dir))
                        shutil.copy(file_path, current_dest_dir)
        log_message(logging.INFO,
                    "Created the distribution at '{}'".format(distribution_dir))

    def update_manifest(self, distribution_dir):
        for release_subdir in ['bin']:
            if release_subdir in self.release_manifest:
                del self.release_manifest[release_subdir]
        for root, dirs, files in os.walk(distribution_dir):
            self.release_manifest.setdefault(os.path.relpath(root, distribution_dir), []).extend(
                [os.path.join(root, f) for f in files])

        log_message(logging.DEBUG,
                    "Effective release manifest:\n" +
                    json.dumps(self.release_manifest, indent=2, sort_keys=True))

    def get_release_file(self):
        """This method does couple of checks before generating the release file name
        - Checks if there are local uncommitted changes.
        - Checks if there are local commits which aren't merged upstream.
        - Reads the base version from the version.txt file and appends to the filename.
        Also fetches the platform the release file is being built and adds that
        to the file name along with commit hash and built type.
        Returns:
            (string): Release file path.
        """
        is_dirty = False
        if check_output(["git", "diff", "origin/master"]).strip():
            log_message(logging.ERROR, "Local changes exists this shouldn't be official release.")
            is_dirty = True
        elif check_output(["git", "log", "origin/master..HEAD", "--oneline"]):
            log_message(logging.ERROR, "Local commits exists this shouldn't be official release.")
            is_dirty = True

        if is_dirty and not self.force:
            prompt_input = raw_input("Continue [Y/n]: ").strip().lower()
            if prompt_input not in ['y', 'yes', '']:
                sys.exit(1)

        cur_commit = check_output(["git", "rev-parse", "HEAD"]).strip()
        release_name = "{}-{}-{}".format(self.base_version, cur_commit, self.build_type)

        system = platform.system().lower()
        if system == "linux":
            system = platform.linux_distribution(full_distribution_name=False)[0].lower()

        release_file_name = "yugabyte-{}-{}-{}-{}.tar.gz".format(
            self.edition, release_name, system, platform.machine().lower())
        return os.path.join(self.build_path, release_file_name)

    def generate_release(self):
        yugabyte_folder_prefix = "yugabyte-{}".format(self.base_version)
        release_file = self.get_release_file()
        response = call(['gtar', 'cvf', release_file,
                         '--transform', 's,^,{}/,'.format(yugabyte_folder_prefix), '.'],
                        cwd=self.distribution_path)
        if response:
            self.log_message(logging.ERROR, "Unable to generate release file.")
            raise RuntimeError("Unable to generate release file.")
        return release_file
