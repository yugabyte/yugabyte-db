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

import glob
import os
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from build_definitions import *

class AwsSdkCppDependency(Dependency):
    def __init__(self):
        super(AwsSdkCppDependency, self).__init__(
            'aws-sdk-cpp', '1.3.57',
            "https://github.com/aws/aws-sdk-cpp/archive/{0}.zip",
            BUILD_GROUP_INSTRUMENTED)
        self.copy_sources = False

    def build(self, builder):
        out_dir = os.path.join(builder.prefix, 'aws-sdk-cpp')
        old_git_dir = os.environ.get('GIT_DIR')
        old_environ = os.environ
        os.environ['GIT_DIR'] = '/tmp'
        try:
          for shared in ['Off', 'On']:
              builder.build_with_cmake(self,
                                       ['-DCMAKE_BUILD_TYPE=Release',
                                        '-DBUILD_SHARED_LIBS={}'.format(shared),
                                        '-DBUILD_ONLY=route53',
                                        '-DENABLE_TESTING=Off',
                                        '-DENABLE_UNITY_BUILD=On',
                                        '-DCMAKE_INSTALL_PREFIX={}'.format(out_dir),
                                        '-DCMAKE_PREFIX_PATH={}'.format(builder.find_prefix)])
              for file in glob.glob(os.path.join(out_dir, 'lib*', 'libaws-cpp-sdk-*.*')):
                  os.rename(file, os.path.join(builder.prefix_lib, os.path.basename(file)))
          dst_include = os.path.join(builder.prefix_include, 'aws')
          remove_path(dst_include)
          os.rename(os.path.join(out_dir, 'include', 'aws'), dst_include)
        finally:
          if old_git_dir is not None:
            os.environ['GIT_DIR'] = old_git_dir
          else:
            del os.environ['GIT_DIR']
