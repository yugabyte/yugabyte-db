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

from sys_detection import local_sys_conf

from typing import Optional

from yb.os_versions import is_compatible_os


def _llvm_url_for_tag(tag: str) -> str:
    return 'https://github.com/yugabyte/build-clang/releases/download/%s/yb-llvm-%s.tar.gz' % (
            tag, tag)


COMPILER_TYPE_TO_ARCH_TO_OS_TYPE_TO_LLVM_URL = {
    'clang11': {
        'x86_64': {
            'centos7': _llvm_url_for_tag('v11.1.0-yb-1-1633099975-130bd22e-centos7-x86_64'),
            'almalinux8': _llvm_url_for_tag('v11.1.0-yb-1-1633143292-130bd22e-almalinux8-x86_64'),
        },
        'aarch64': {
            'centos8': _llvm_url_for_tag('v11.1.0-yb-1-1633544021-130bd22e-centos8-aarch64'),
        },
    },
    'clang12': {
        'x86_64': {
            'centos7': _llvm_url_for_tag('v12.0.1-yb-1-1633099823-bdb147e6-centos7-x86_64'),
            'almalinux8': _llvm_url_for_tag('v12.0.1-yb-1-1633143152-bdb147e6-almalinux8-x86_64'),
        },
    }
}


def get_llvm_url(compiler_type: str) -> Optional[str]:
    os_type_to_llvm_url = (
        COMPILER_TYPE_TO_ARCH_TO_OS_TYPE_TO_LLVM_URL.get(compiler_type) or {}
    ).get(local_sys_conf().architecture)
    if os_type_to_llvm_url is None:
        return None

    os_type = local_sys_conf().short_os_name_and_version()
    if os_type in os_type_to_llvm_url:
        return os_type_to_llvm_url[os_type]

    candidate_urls = [
        os_type_to_llvm_url[os_type_key]
        for os_type_key in os_type_to_llvm_url
        if is_compatible_os(os_type_key, os_type)
    ]
    if len(candidate_urls) > 1:
        raise ValueError("Ambiguous LLVM URLs: %s" % candidate_urls)
    if not candidate_urls:
        return None
    return candidate_urls[0]
