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

# This module provides utilities for manupulating compiler arguments and extracting information
# from lists of compiler arguments.

import functools

from yugabyte.command_util import shlex_join

from collections import defaultdict

from typing import List, DefaultDict, Optional, Any, Tuple, Hashable, Union, Iterable


def get_include_path_arg(include_path: str) -> str:
    return '-I%s' % include_path


def split_preprocessor_definition_flag(arg: str) -> Tuple[str, Optional[str]]:
    '''
    >>> split_preprocessor_definition_flag('-DBOOST_BIND_NO_PLACEHOLDERS')
    ('BOOST_BIND_NO_PLACEHOLDERS', None)
    >>> split_preprocessor_definition_flag('-DBOOST_BIND_NO_PLACEHOLDERS=1')
    ('BOOST_BIND_NO_PLACEHOLDERS', '1')
    >>> split_preprocessor_definition_flag('-D_GLIBCXX_EXTERN_TEMPLATE=0')
    ('_GLIBCXX_EXTERN_TEMPLATE', '0')
    '''
    assert arg.startswith('-D'), 'Expected a preprocessor definition flag, got %s' % arg
    split_on_equal = arg[2:].split('=', 1)
    def_name: Optional[str] = None
    def_value: Optional[str] = None
    if len(split_on_equal) == 1:
        def_name = split_on_equal[0]
    elif len(split_on_equal) == 2:
        def_name, def_value = split_on_equal
    assert def_name is not None
    return def_name, def_value


def get_preprocessor_definition_values(args: List[str]) -> DefaultDict[str, List[str]]:
    """
    Given a list of compiler arguments, returns a dictionary mapping preprocessor definitions
    specified in the command line to their values, in the order they appeared.
    """
    name_to_values: DefaultDict[str, List[str]] = defaultdict(list)
    for arg in args:
        if arg.startswith('-D'):
            def_name, def_value = split_preprocessor_definition_flag(arg)
            if def_value is None:
                def_value = '1'
            if def_value not in name_to_values[def_name]:
                name_to_values[def_name].append(def_value)
    return name_to_values


def split_args_into_reorderable_groups(args: Iterable[str]) -> Tuple[Tuple[str, ...], ...]:
    """
    Sometimes, to test whether argument lists are requivalent, it is necessary to reorder the
    arguments in the list to some extent. This function splits the given list of arguments into
    groups of arguments that can be reordered across groups, but not within each group.

    Currently, the only type of such groups are preprocessor definitions. For each preprocessor
    definition, we create a separate group containing the -D flag and all its values.
    We do not attempt to remove duplicate options setting the same flag within a group.

    >>> split_args_into_reorderable_groups(['clang', '-DFOO', '-DBAR', '-DFOO=1', '-DBAR=3'])
    (('clang',), ('-DBAR', '-DBAR=3'), ('-DFOO', '-DFOO=1'))
    """
    groups_for_preprocessor_definitions = defaultdict(list)
    other_args = []
    for arg in args:
        if arg.startswith('-D'):
            def_name, def_value = split_preprocessor_definition_flag(arg)
            flag = '-D%s' % def_name
            if def_value is not None:
                flag += '=%s' % def_value
            groups_for_preprocessor_definitions[def_name].append(flag)
        else:
            other_args.append(arg)
    groups: List[Tuple[str, ...]] = [tuple(other_args)]
    for def_name, values in sorted(groups_for_preprocessor_definitions.items()):
        groups.append(tuple(values))
    return tuple(groups)


@functools.total_ordering
class CompilerArguments:
    """
    A wrapper around a compiler command line with some functionality allowing to manupilate it.
    The first argument is the compiler path.
    """

    # List of arguments. The first argument is the compiler path. We allow this to be either a list
    # or a tuple for sanity-checking purposes. Once the compiler argument list is finalized, we
    # can "freeze" it into a tuple and use it for hashing and comparison.
    args: Union[List[str], Tuple[str, ...]]

    # Index of the argument following by the -o flag, indicating the output file path of the
    # compiler command.
    output_index: Optional[int]

    def __init__(self, args: List[str]) -> None:
        self.args = args
        self.output_index = None

    def get_output_index(self) -> int:
        """
        Returns the index of argument following by the -o flag in the command line, indicating
        the output file path of the compiler command. Raises an exception if the command line
        does not contain a -o flag or contains multiple such flags.
        """
        if self.output_index is not None:
            return self.output_index

        for i in range(len(self.args) - 1):
            if self.args[i] == '-o':
                if self.output_index is not None:
                    raise ValueError(
                        "Compiler command line contains multiple -o flags: %s" % self.args)
                self.output_index = i + 1

        if not self.output_index:
            raise ValueError(
                "Compiler command line does not contain a -o flag followed by an argument: %s" %
                self.args)

        return self.output_index

    def get_output_path(self) -> str:
        """
        Returns the output file path of the compiler command.
        """
        return self.args[self.get_output_index()]

    def set_output_path(self, new_output_path: str) -> None:
        """
        Sets the output file path of the compiler command.
        """
        assert isinstance(self.args, list)
        self.args[self.get_output_index()] = new_output_path

    def append_to_output_path(self, suffix: str) -> str:
        """
        Appends the given suffix to the output file path of the compiler command,
        """
        new_output_path = self.get_output_path() + suffix
        self.set_output_path(new_output_path)
        return new_output_path

    def remove_flag(self, flag: str) -> None:
        """
        Removes all occurrences of the given flag from the command line. If the flag is not found,
        does nothing.
        """
        self.args = [arg for arg in self.args if arg != flag]

    def remove_dependency_related_flags(self) -> None:
        """
        Removes flags related to writing dependencies to files. When invoking the compiler or the
        preprocessor outside of main build system, we don't need to write these files.

        From https://clang.llvm.org/docs/ClangCommandLineReference.html:
        -MD, --write-dependencies        Write a depfile containing user and system headers.
        -MF<file>                        Write depfile output from -MMD, -MD, -MM, or -M to <file>.
        -MMD, --write-user-dependencies  Write a depfile containing user headers.
        """
        self.remove_flag('-MMD')
        self.remove_flag('-MP')
        self.remove_flag_with_argument('-MF')

    def remove_flag_with_argument(self, flag: str) -> None:
        """
        Removes the first occurrence of the given flag from the command line, along with its
        argument. If the flag is not found, does nothing.
        """
        assert isinstance(self.args, list)
        for i in range(len(self.args) - 1):
            if self.args[i] == flag:
                del self.args[i:i + 2]
                return

    def __eq__(self, other: Any) -> bool:
        if not isinstance(other, CompilerArguments):
            return False
        return self.args == other.args

    def __ne__(self, other: Any) -> bool:
        return not self.__eq__(other)

    def __hash__(self) -> int:
        self.freeze()
        return hash(self.args)

    def __lt__(self, other: Any) -> bool:
        return self.args < other.args

    def equivalence_key(self) -> Hashable:
        '''
        Returns a key that we could group commands by to determine whether they are equivalent.
        The equivalence detection is not guaranteed to be perfect -- it will be refined only as
        needed.
        '''
        return split_args_into_reorderable_groups(self.args)

    def freeze(self) -> None:
        '''
        Freezes the argument list, making it immutable and suitable for hashing and comparison.
        '''
        if isinstance(self.args, list):
            self.args = tuple(self.args)

    def extend(self, args: Iterable[str]) -> None:
        '''
        Extends the argument list with the given arguments.
        '''
        assert isinstance(self.args, list)
        self.args.extend(args)

    def mutable_arg_list(self) -> List[str]:
        '''
        Returns the mutable list of arguments. This is useful for modifying the arguments.
        '''
        assert isinstance(self.args, list)
        return self.args

    def immutable_arg_list(self) -> List[str]:
        """
        Returns the immutable list of arguments. This is useful for passing the arguments to
        functions that expect a list.
        """
        return list(self.args)

    def get_cmd_line_str(self) -> str:
        '''
        Returns the command line as a string.
        '''
        return shlex_join(self.immutable_arg_list())
