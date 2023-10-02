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
#

import argparse

from typing import List, Set, Tuple
from sys_detection import is_linux


COMPONENT_DESCRIPTIONS = {
    'yugabyted_ui': 'yugabyted UI',
    'odyssey': 'Odyssey PostgreSQL connection pooler',
}

COMPONENTS_ENABLED_BY_DEFAULT: Set[str] = {'yugabyted_ui'}
COMPONENTS_ENABLED_BY_DEFAULT_ON_LINUX: Set[str] = {'odyssey'}


class OptionalComponents:
    yugabyted_ui_enabled: bool
    odyssey_enabled: bool

    def __init__(
            self,
            yugabyted_ui_enabled: bool,
            odyssey_enabled: bool):
        self.yugabyted_ui_enabled = yugabyted_ui_enabled
        self.odyssey_enabled = odyssey_enabled

    @staticmethod
    def all_disabled() -> 'OptionalComponents':
        return OptionalComponents(
            yugabyted_ui_enabled=False,
            odyssey_enabled=False)

    def get_yb_build_args(self) -> List[str]:
        yb_build_args = []
        for component_name in COMPONENT_DESCRIPTIONS.keys():
            if getattr(self, component_name + '_enabled'):
                arg_prefix = '--with-'
            else:
                arg_prefix = '--no-'
            # Use dashes instead of underscores in the argument name for yb_build.sh.
            yb_build_args.append(arg_prefix + component_name.replace('_', '-'))
        return yb_build_args

    def __str__(self) -> str:
        return 'OptionalComponents(' + ', '.join(
            '{}={}'.format(
                component_name, getattr(self, component_name + '_enabled'))
            for component_name in COMPONENT_DESCRIPTIONS.keys()
        ) + ')'


def get_optional_component_default(component_name: str) -> Tuple[bool, str]:
    """
    Returns whether the given component is enabled or disabled by default, and a string description
    of the reason why it is so.
    """
    if component_name in COMPONENTS_ENABLED_BY_DEFAULT:
        return True, 'enabled by default'

    if is_linux() and component_name in COMPONENTS_ENABLED_BY_DEFAULT_ON_LINUX:
        return True, 'enabled by default on Linux'

    return False, 'disabled by default'


def add_optional_component_arguments(arg_parser: argparse.ArgumentParser) -> None:
    for component_name, component_description in COMPONENT_DESCRIPTIONS.items():
        for enabling in [True, False]:
            if enabling:
                flag_prefixes = ['with_']
            else:
                flag_prefixes = ['no_', 'skip_']

            flag_names = [
                prefix + component_name
                for prefix in flag_prefixes
            ]
            if component_name == 'yugabyted_ui' and not enabling:
                # For backward compatibility in yb_release.py, we also support the old flag name.
                flag_names.append('skip_yugabyted_ui_build')
            enabled_by_default, default_value_description = get_optional_component_default(
                    component_name)
            help_text = '%s building %s (%s)' % (
                'Enable' if enabling else 'Disable',
                component_description,
                default_value_description
            )
            arg_parser.add_argument(
                *['--' + flag_name for flag_name in flag_names],
                action='store_' + str(enabling).lower(),
                help=help_text,
                dest=component_name + '_enabled',
                # Important to explicity specify this so we can apply the default value later.
                default=None)


def optional_components_from_args(args: argparse.Namespace) -> OptionalComponents:
    constructor_args = {}
    for component_name in COMPONENT_DESCRIPTIONS.keys():
        var_name = component_name + '_enabled'
        is_enabled = getattr(args, var_name)
        if is_enabled is None:
            is_enabled = get_optional_component_default(component_name)[0]
        constructor_args[var_name] = is_enabled

    return OptionalComponents(**constructor_args)
