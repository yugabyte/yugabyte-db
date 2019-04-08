#
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied. See the License for the specific language governing permissions and limitations
# under the License.
#

import os
import subprocess
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from build_definitions import *

class LLVM6Dependency(Dependency):
    VERSION = '6.0.1'

    def __init__(self):
        url_prefix = "http://releases.llvm.org/{0}/"
        super(LLVM6Dependency, self).__init__(
                'llvm', LLVM6Dependency.VERSION, url_prefix + 'llvm-{0}.src.tar.xz',
                BUILD_GROUP_COMMON)
        self.dir += ".src"
        self.extra_downloads = [
            ExtraDownload('cfe', self.version, url_prefix + 'cfe-{0}.src.tar.xz',
                                        'tools', ['mv', 'cfe-{}.src'.format(self.version), 'cfe']),
            ExtraDownload('compiler-rt', self.version, url_prefix + 'compiler-rt-{0}.src.tar.xz',
                          'projects',
                          ['mv', 'compiler-rt-{}.src'.format(self.version), 'compiler-rt']),
        ]

        self.copy_sources = False

    def build(self, builder):
        # The LLVM build can fail if a different version is already installed
        # in the install prefix. It will try to link against that version instead
        # of the one being built.
        subprocess.check_call(
                "rm -Rf {0}/include/{{llvm*,clang*}} {0}/lib/lib{{LLVM,LTO,clang}}* {0}/lib/clang/ "
                        "{0}/lib/cmake/{{llvm,clang}}".format(builder.prefix), shell=True)

        python_executable = which('python')
        if not os.path.exists(python_executable):
            fatal("Could not find Python -- needed to build LLVM.")

        cxx_flags = builder.compiler_flags + builder.cxx_flags + builder.ld_flags
        if '-g' in cxx_flags:
            cxx_flags.remove('-g')

        builder.build_with_cmake(self,
                                 ['-DCMAKE_BUILD_TYPE=Release',
                                  '-DCMAKE_INSTALL_PREFIX={}'.format(builder.prefix),
                                  '-DLLVM_INCLUDE_DOCS=OFF',
                                  '-DLLVM_INCLUDE_EXAMPLES=OFF',
                                  '-DLLVM_INCLUDE_TESTS=OFF',
                                  '-DLLVM_INCLUDE_UTILS=OFF',
                                  '-DLLVM_TARGETS_TO_BUILD=X86',
                                  '-DLLVM_ENABLE_RTTI=ON',
                                  '-DCMAKE_CXX_FLAGS={}'.format(" ".join(cxx_flags)),
                                  '-DPYTHON_EXECUTABLE={}'.format(python_executable)])

        # Create a link from Clang to thirdparty/clang-toolchain. This path is used
        # for all Clang invocations. The link can't point to the Clang installed in
        # the prefix directory, since this confuses CMake into believing the
        # thirdparty prefix directory is the system-wide prefix, and it omits the
        # thirdparty prefix directory from the rpath of built binaries.
        link_path = os.path.join(builder.tp_dir, 'clang-toolchain')
        remove_path(link_path)
        list_dest = os.path.relpath(os.getcwd(), builder.tp_dir)
        log("Link {} => {}".format(link_path, list_dest))
        os.symlink(list_dest, link_path)
