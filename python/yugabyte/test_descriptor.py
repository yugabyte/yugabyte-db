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

import re
import copy
import os
import enum
import logging

from functools import total_ordering
from typing import Optional, cast, Tuple, Iterable

from yugabyte import file_util


# This is used to separate relative binary path from gtest_filter for C++ tests in what we call
# a "test descriptor" (a string that identifies a particular test).
#
# This must match the constant with the same name in common-test-env.sh.
TEST_DESCRIPTOR_SEPARATOR = ":::"

TEST_DESCRIPTOR_ATTEMPT_PREFIX = TEST_DESCRIPTOR_SEPARATOR + 'attempt_'

TEST_DESCRIPTOR_ATTEMPT_INDEX_RE = re.compile(
    r'^(.*)' + TEST_DESCRIPTOR_ATTEMPT_PREFIX + r'(\d+)$')

JAVA_TEST_DESCRIPTOR_RE = re.compile(r'^([a-z0-9-]+)/src/test/(?:java|scala)/(.*)$')

BINARY_AND_TEST_NAME_SEPARATOR = "__"


class LanguageOfTest(enum.Enum):
    CPP = enum.auto()
    JAVA = enum.auto()

    def __str__(self) -> str:
        if self == LanguageOfTest.CPP:
            return "C++"
        if self == LanguageOfTest.JAVA:
            return "Java"
        raise ValueError("Unknown test language, cannot convert to string: " + str(self))

    def __repr__(self) -> str:
        return repr(str(self))

    @staticmethod
    def from_str(language: str) -> 'LanguageOfTest':
        language = language.lower()
        if language in ["c++", "cpp", "cxx", "cc"]:
            return LanguageOfTest.CPP
        if language == "java":
            return LanguageOfTest.JAVA
        raise ValueError("Could not parse test language: " + language)


@total_ordering
class TestDescriptor:
    """
    A "test descriptor" identifies a particular test we could run on a distributed test worker.
    A string representation of a "test descriptor" is an optional "attempt_<index>:::" followed by
    one of the options below:
    - A C++ test program name relative to the build root. This implies running all tests within
      the test program. This has the disadvantage that a failure of one of those tests will cause
      the rest of tests not to be run.
    - A C++ test program name relative to the build root followed by the ':::' separator and the
      gtest filter identifying a test within that test program,
    - A string like 'com.yugabyte.jedis.TestYBJedis#testPool[1]' describing a Java test. This is
      something that could be passed directly to the -Dtest=... Maven option.
    - A Java test class source path (including .java/.scala extension) relative to the "java"
      directory in the YugabyteDB source tree.
    """
    descriptor_str_without_attempt_index: str
    attempt_index: int
    is_jvm_based: bool
    language: str
    args_for_run_test: str
    rel_error_output_path: str

    def __init__(self, descriptor_str: str) -> None:
        self.descriptor_str = descriptor_str

        attempt_index_match = TEST_DESCRIPTOR_ATTEMPT_INDEX_RE.match(descriptor_str)
        if attempt_index_match:
            self.attempt_index = int(attempt_index_match.group(2))
            self.descriptor_str_without_attempt_index = attempt_index_match.group(1)
        else:
            self.attempt_index = 1
            self.descriptor_str_without_attempt_index = descriptor_str

        self.is_jvm_based = False
        is_mvn_compatible_descriptor = False

        if len(self.descriptor_str.split('#')) == 2:
            self.is_jvm_based = True
            # Could be Scala, but as of 08/2018 we only have Java tests in the repository.
            self.language = 'Java'
            is_mvn_compatible_descriptor = True
        elif self.descriptor_str.endswith('.java'):
            self.is_jvm_based = True
            self.language = 'Java'
        elif self.descriptor_str.endswith('.scala'):
            self.is_jvm_based = True
            self.language = 'Scala'

        if self.is_jvm_based:
            # This is a Java/Scala test.
            if is_mvn_compatible_descriptor:
                # This is a string of the form "com.yugabyte.jedis.TestYBJedis#testPool[1]".
                self.args_for_run_test = self.descriptor_str
                output_file_name = self.descriptor_str
            else:
                # The "test descriptors string " is the Java source file path relative to the "java"
                # directory.
                java_descriptor_match = JAVA_TEST_DESCRIPTOR_RE.match(
                    self.descriptor_str_without_attempt_index)
                if java_descriptor_match is None:
                    raise ValueError(
                        f"java/..."
                        f"could not be parsed using the regular expression "
                        f"{JAVA_TEST_DESCRIPTOR_RE}")

                mvn_module, package_and_class_with_slashes = java_descriptor_match.groups()

                package_and_class = package_and_class_with_slashes.replace('/', '.')
                self.args_for_run_test = "{} {}".format(mvn_module, package_and_class)
                output_file_name = package_and_class
        else:
            self.language = 'C++'
            test_name: Optional[str]

            # This is a C++ test.
            if TEST_DESCRIPTOR_SEPARATOR in self.descriptor_str_without_attempt_index:
                rel_test_binary, test_name = self.descriptor_str_without_attempt_index.split(
                    TEST_DESCRIPTOR_SEPARATOR)
            else:
                rel_test_binary = self.descriptor_str_without_attempt_index
                test_name = None

            # Arguments for run-test.sh.
            # - The absolute path to the test binary (the test descriptor only contains the relative
            #   path).
            # - Optionally, the gtest filter within the test program.
            self.args_for_run_test = rel_test_binary
            if test_name:
                self.args_for_run_test += " " + test_name
            output_file_name = rel_test_binary
            if test_name:
                output_file_name += BINARY_AND_TEST_NAME_SEPARATOR + test_name

        output_file_name = re.sub(r'[\[\]/#]', '_', output_file_name)
        self.rel_error_output_path = os.path.join(
            'yb-test-logs', output_file_name + '__error.log')

    def __str__(self) -> str:
        if self.attempt_index == 1:
            return self.descriptor_str_without_attempt_index
        return ''.join([
            self.descriptor_str_without_attempt_index,
            TEST_DESCRIPTOR_ATTEMPT_PREFIX,
            str(self.attempt_index)
        ])

    def str_for_file_name(self) -> str:
        return str(self).replace('/', '__').replace(':', '_')

    def __eq__(self, other: object) -> bool:
        other_descriptor = cast(TestDescriptor, other)
        return self.descriptor_str == other_descriptor.descriptor_str

    def __hash__(self) -> int:
        return hash(self.descriptor_str)

    def __ne__(self, other: object) -> bool:
        return not (self == other)

    def __lt__(self, other: object) -> bool:
        other_descriptor = cast(TestDescriptor, other)
        return self.descriptor_str < other_descriptor.descriptor_str

    def with_attempt_index(self, attempt_index: int) -> 'TestDescriptor':
        assert attempt_index >= 1
        copied = copy.copy(self)
        copied.attempt_index = attempt_index
        # descriptor_str is just the cached version of the string representation, with the
        # attempt_index included (if it is greater than 1).
        copied.descriptor_str = str(copied)
        return copied


@total_ordering
class SimpleTestDescriptor:
    language: LanguageOfTest
    cxx_rel_test_binary: Optional[str]
    class_name: Optional[str]
    test_name: Optional[str]

    def __init__(
            self,
            language: LanguageOfTest,
            cxx_rel_test_binary: Optional[str],
            class_name: Optional[str],
            test_name: Optional[str]) -> None:
        self.language = language
        self.test_name = test_name
        self.class_name = class_name
        self.cxx_rel_test_binary = cxx_rel_test_binary

    @staticmethod
    def parse(descriptor_str: str) -> 'SimpleTestDescriptor':
        items = descriptor_str.split(TEST_DESCRIPTOR_SEPARATOR)
        assert len(items) in [1, 2], "Invalid test descriptor string: %s" % descriptor_str

        language: LanguageOfTest
        cxx_rel_test_binary: Optional[str] = None
        cpp_class_and_test_name: Optional[str] = None
        class_name: Optional[str] = None
        test_name: Optional[str] = None
        if len(items) == 1 and not descriptor_str.startswith('tests-'):
            language = LanguageOfTest.JAVA
            class_name, test_name = descriptor_str.split('#')
        else:
            language = LanguageOfTest.CPP
            if len(items) == 1:
                cxx_rel_test_binary = items[0]
                test_name = None
            else:
                cxx_rel_test_binary, cpp_class_and_test_name = items
                class_name, test_name = cpp_class_and_test_name.split('.')
        return SimpleTestDescriptor(
            language=language,
            cxx_rel_test_binary=cxx_rel_test_binary,
            class_name=class_name,
            test_name=test_name)

    def __eq__(self, other: object) -> bool:
        other_test_descriptor = cast(SimpleTestDescriptor, other)
        return (
            self.language == other_test_descriptor.language and
            self.cxx_rel_test_binary == other_test_descriptor.cxx_rel_test_binary and
            self.class_name == other_test_descriptor.class_name and
            self.test_name == other_test_descriptor.test_name)

    def __hash__(self) -> int:
        return hash((
            self.language,
            self.cxx_rel_test_binary,
            self.class_name,
            self.test_name))

    def comparison_key(self) -> Tuple[int, str, str, str]:
        return (self.language.value,
                self.cxx_rel_test_binary or '',
                self.class_name or '',
                self.test_name or '')

    def __lt__(self, other: str) -> bool:
        other_test_descriptor = cast(SimpleTestDescriptor, other)
        return self.comparison_key() < other_test_descriptor.comparison_key()

    def __repr__(self) -> str:
        return 'SimpleTestDescriptor(%s)' % ', '.join([
            '%s=%s' % (
                field_name,
                repr(getattr(self, field_name))
            ) for field_name in [
                'language',
                'cxx_rel_test_binary',
                'class_name',
                'test_name'
            ] if getattr(self, field_name) is not None
        ])

    def __str__(self) -> str:
        """
        Produces a representation that can be parsed by the parse method.
        """
        s = ''
        if self.language == LanguageOfTest.CPP:
            assert self.cxx_rel_test_binary is not None
            s += self.cxx_rel_test_binary
        if self.class_name:
            if s:
                s += TEST_DESCRIPTOR_SEPARATOR
            s += self.class_name
            if self.test_name:
                s += '.' if self.language == LanguageOfTest.CPP else '#'
                s += self.test_name
        return s


def write_test_descriptors_to_file(
        output_path: Optional[str],
        test_descriptors: Iterable[SimpleTestDescriptor],
        description_for_log: Optional[str] = None) -> None:
    if output_path is None:
        # The real use case for this function is such that the output file creation is optional.
        return
    out_lines = sorted([str(t) for t in test_descriptors])
    out_str = '\n'.join(out_lines) + '\n'
    if description_for_log:
        logging.info("Writing the list of %s to %s", description_for_log, output_path)
    file_util.write_file(out_str, output_path)
