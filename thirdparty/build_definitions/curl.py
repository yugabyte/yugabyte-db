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
import sys

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from build_definitions import *

class CurlDependency(Dependency):
    def __init__(self):
        super(CurlDependency, self).__init__(
                'curl', '7.32.0', "https://curl.haxx.se/download/curl-{0}.tar.gz",
                BUILD_GROUP_COMMON)
        self.copy_sources = True

    def build(self, builder):
        disabled_features = ['ftp', 'file', 'ldap', 'ldaps', 'rtsp', 'dict', 'telnet', 'tftp',
                             'pop3', 'imap', 'smtp', 'gopher', 'manual', 'librtmp', 'ipv6']
        extra_args = ['--disable-' + feature for feature in disabled_features]

        openssl_dir = get_openssl_dir()
        if openssl_dir:
            extra_args.append('--with-ssl=%s' % openssl_dir)

        builder.build_with_configure(builder.log_prefix(self), extra_args)
