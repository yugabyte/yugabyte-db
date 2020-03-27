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

class LLVMDependency(Dependency):
    VERSION = '7.0.1'

    def __init__(self):
        url_prefix="http://releases.llvm.org/{0}/"
        super(LLVMDependency, self).__init__(
                'llvm', LLVMDependency.VERSION, url_prefix + 'llvm-{0}.src.tar.xz',
                BUILD_GROUP_COMMON)
        self.dir += ".src"
        self.extra_downloads = [
            ExtraDownload('cfe', self.version, url_prefix + 'cfe-{0}.src.tar.xz',
                                        'tools', ['mv', 'cfe-{}.src'.format(self.version), 'cfe']),
            ExtraDownload('compiler-rt', self.version, url_prefix + 'compiler-rt-{0}.src.tar.xz',
                          'projects',
                          ['mv', 'compiler-rt-{}.src'.format(self.version), 'compiler-rt']),
            ExtraDownload(
                'clang-tools-extra', self.version, url_prefix + 'clang-tools-extra-{0}.src.tar.xz',
                'tools/cfe/tools',
                ['mv', 'clang-tools-extra-{}.src'.format(self.version), 'extra']),
        ]

        self.copy_sources = False

    def build(self, builder):
        prefix = builder.get_prefix('llvm7')

        # The LLVM build can fail if a different version is already installed
        # in the install prefix. It will try to link against that version instead
        # of the one being built.
        subprocess.check_call(
                "rm -Rf {0}/include/{{llvm*,clang*}} {0}/lib/lib{{LLVM,LTO,clang}}* {0}/lib/clang/ "
                        "{0}/lib/cmake/{{llvm,clang}}".format(prefix), shell=True)

        python_executable = which('python')
        if not os.path.exists(python_executable):
            fatal("Could not find Python -- needed to build LLVM.")

        cxx_flags = builder.compiler_flags + builder.cxx_flags + builder.ld_flags
        if '-g' in cxx_flags:
            cxx_flags.remove('-g')

        builder.build_with_cmake(self,
                                 ['-DCMAKE_BUILD_TYPE=Release',
                                  '-DCMAKE_INSTALL_PREFIX={}'.format(prefix),
                                  '-DLLVM_INCLUDE_DOCS=OFF',
                                  '-DLLVM_INCLUDE_EXAMPLES=OFF',
                                  '-DLLVM_INCLUDE_TESTS=OFF',
                                  '-DLLVM_INCLUDE_UTILS=OFF',
                                  '-DLLVM_TARGETS_TO_BUILD=X86',
                                  '-DLLVM_ENABLE_RTTI=ON',
                                  '-DCMAKE_CXX_FLAGS={}'.format(" ".join(cxx_flags)),
                                  '-DPYTHON_EXECUTABLE={}'.format(python_executable),
                                  '-DCLANG_BUILD_EXAMPLES=ON'
                                 ],
                                 use_ninja='auto')

        link_path = os.path.join(builder.tp_dir, 'clang-toolchain')
        remove_path(link_path)
        list_dest = os.path.relpath(prefix, builder.tp_dir)
        log("Link {} => {}".format(link_path, list_dest))
        os.symlink(list_dest, link_path)

    def should_build(self, builder):
        return builder.will_need_clang()
