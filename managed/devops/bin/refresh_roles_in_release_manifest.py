#!/usr/bin/env python
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
#
# Starting from a set of "seed" playbooks, find the set of YB and third-party roles that need to
# be included in the devops package. Also "normalizes" the manifest file (sorts keys). This should
# be run from time to time to ensure that we're shipping the exact set of roles we need. This could
# also be incorporated in the devops testing pipeline.

import glob
import json
import logging
import os
import yaml

DEVOPS_HOME = os.path.dirname(os.path.abspath(os.path.dirname(__file__)))
YB_ROLES_DIR = os.path.join(DEVOPS_HOME, 'roles')
THIRDPARTY_ROLES_DIR = os.path.join(DEVOPS_HOME, 'third-party', 'roles')
SERVER_TYPE_MACRO = "{{ server_type }}"


def extract_role_name(role_item):
    # Roles can be specified in dependencies in two ways:
    # - As a single string (the role name)
    # - As a dictionary with a "role" field and other fields customizing the role
    #   behavior.
    if isinstance(role_item, str):
        return role_item
    else:
        return role_item['role']


class RoleExtractor:
    def __init__(self):
        self.yb_roles = set()
        self.thirdparty_roles = set()

    def add_role(self, role_name):
        if role_name in self.yb_roles or \
           role_name in self.thirdparty_roles:
            return

        if SERVER_TYPE_MACRO in role_name:
            # This is for the logic in yb-server-provision.yml where we are dynamically constructing
            # a role name.
            for server_type in ['cluster-server']:
                self.add_role(role_name.replace(SERVER_TYPE_MACRO, server_type))
            return

        looked_at = []
        for role_dir in [YB_ROLES_DIR, THIRDPARTY_ROLES_DIR]:
            yb_role_path = os.path.join(role_dir, role_name)
            if os.path.isdir(yb_role_path):
                logging.info("Fould role {} at {}".format(role_name, yb_role_path))
                if role_dir == YB_ROLES_DIR:
                    self.yb_roles.add(role_name)
                else:
                    self.thirdparty_roles.add(role_name)
                self.process_role(yb_role_path)
                return
            looked_at.append(yb_role_path)

        raise RuntimeError("Role not found: {} (looked at {})".format(role_name, looked_at))

    def process_role(self, role_path):
        meta_main_path = os.path.join(role_path, 'meta', 'main.yml')
        if os.path.isfile(meta_main_path):
            with open(meta_main_path) as meta_main_file:
                meta_main = yaml.load(meta_main_file.read())
            deps = meta_main.get('dependencies', None)
            if deps:
                for dep in deps:
                    self.add_role(extract_role_name(dep))

    def process_playbook(self, playbook_path):
        playbook_name = os.path.basename(playbook_path)
        if playbook_name == 'ansible_requirements.yml':
            logging.info("Skipping a non-playbook YAML file {}".format(playbook_path))
            return

        logging.info("Processing playbook: {}".format(playbook_path))
        with open(playbook_path) as playbook_file:
            playbook = yaml.load(playbook_file.read())
        for item in playbook:
            roles = item.get('roles', None)
            if roles:
                for role_item in roles:
                    self.add_role(extract_role_name(role_item))


def main():
    log_level = logging.INFO
    logging.basicConfig(
        level=log_level,
        format="[%(filename)s:%(lineno)d] %(asctime)s %(levelname)s: %(message)s")
    extractor = RoleExtractor()
    release_manifest_path = os.path.join(DEVOPS_HOME, 'yb_release_manifest.json')
    with open(release_manifest_path) as input_file:
        release_manifest = json.load(input_file)

    for playbook_name in release_manifest['.']:
        if playbook_name.endswith('.yml'):
            logging.info("Found a playbook-like entry in '.' in release manifest: {}".format(
                    playbook_name))
            if playbook_name == '*.yml':
                for playbook_path in glob.glob(os.path.join(DEVOPS_HOME, '*.yml')):
                    extractor.process_playbook(playbook_path)
            else:
                extractor.process_playbook(os.path.join(DEVOPS_HOME, playbook_name))

    release_manifest['third-party/roles'] = [
            'third-party/roles/{}'.format(role_name) for role_name in
            sorted(extractor.thirdparty_roles)]
    release_manifest['roles'] = [
            'roles/{}'.format(role_name) for role_name in sorted(extractor.yb_roles)]
    logging.info("Writing {}".format(release_manifest_path))
    with open(release_manifest_path, 'w') as output_file:
        lines = json.dumps(release_manifest, indent=2, sort_keys=True).split("\n")
        lines = [l.rstrip() for l in lines]
        output_file.write("\n".join(lines) + "\n")

    with open(os.path.join(DEVOPS_HOME, 'ansible_requirements.yml')) as ansible_req_input_file:
        ansible_requirements = yaml.load(ansible_req_input_file.read())

    prod_ansible_requirements = []
    for requirement in ansible_requirements:
        thirdparty_role_name = requirement['name']
        if thirdparty_role_name in extractor.thirdparty_roles:
            logging.info(
                    "INCLUDING third-party role {} in production ansible_requirements.yml".format(
                            thirdparty_role_name))
            prod_ansible_requirements.append(requirement)
        else:
            logging.info(
                    "Skipping third-party role {} from production ansible_requirements.yml".format(
                            thirdparty_role_name))
    prod_ansible_requirements = sorted(prod_ansible_requirements,
                                       key=lambda requirement: requirement['name'])
    prod_ansible_requirements_path = os.path.join(DEVOPS_HOME, 'ansible_requirements_prod.yml')
    with open(prod_ansible_requirements_path, 'w') as new_ansible_requirements_file:
        new_ansible_requirements_file.write(
                "# This file is generated automatically by {}. DO NOT EDIT!\n".format(
                    os.path.basename(__file__)) +
                yaml.dump(prod_ansible_requirements, default_flow_style=False))


if __name__ == '__main__':
    main()
