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

class CryptBlowfishDependency(Dependency):
    def __init__(self):
        super(CryptBlowfishDependency, self).__init__(
                'crypt_blowfish', '79ee670d1a2977328c481d6578387928ff92896a',
                'https://github.com/YugaByte/crypt_blowfish/archive/{0}.tar.gz',
                BUILD_GROUP_INSTRUMENTED)
        self.copy_sources = True

    def build(self, builder):
        log_prefix = builder.log_prefix(self)
        log_output(log_prefix, ['make', 'clean'])
        log_output(log_prefix, ['make'])
        crypt_blowfish_include_dir = os.path.join(builder.prefix_include, 'crypt_blowfish')
        mkdir_if_missing(crypt_blowfish_include_dir)
        # Copy over all the headers into a generic include/ directory.
        subprocess.check_call('rsync -av *.h {}'.format(crypt_blowfish_include_dir), shell=True)
        subprocess.check_call('ar r libcrypt_blowfish.a *.o', shell=True)
        os.rename('libcrypt_blowfish.a', os.path.join(builder.prefix_lib, 'libcrypt_blowfish.a'))
