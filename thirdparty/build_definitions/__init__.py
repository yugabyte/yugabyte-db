#
# Copyright (c) YugaByte, Inc.
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

import os
import sys

BUILD_DEFINITIONS_DIR = os.path.realpath(os.path.dirname(__file__))
print BUILD_DEFINITIONS_DIR
sys.path = filter(lambda p: os.path.realpath(p) != BUILD_DEFINITIONS_DIR, sys.path)

import importlib
import pkgutil
import platform
import shutil
import subprocess
import traceback


YELLOW_COLOR="\033[0;33m"
RED_COLOR="\033[0;31m"
CYAN_COLOR="\033[0;36m"
NO_COLOR="\033[0m"
SEPARATOR = "-" * 80


BUILD_GROUP_COMMON = 1
BUILD_GROUP_INSTRUMENTED = 2


BUILD_TYPE_COMMON = 'common'
BUILD_TYPE_UNINSTRUMENTED = 'uninstrumented'
BUILD_TYPE_GCC8_UNINSTRUMENTED = 'gcc8_uninstrumented'
BUILD_TYPE_ASAN = 'asan'
BUILD_TYPE_TSAN = 'tsan'
BUILD_TYPE_CLANG_UNINSTRUMENTED = 'clang_uninstrumented'
BUILD_TYPES = [BUILD_TYPE_COMMON, BUILD_TYPE_UNINSTRUMENTED, BUILD_TYPE_CLANG_UNINSTRUMENTED,
               BUILD_TYPE_ASAN, BUILD_TYPE_TSAN, BUILD_TYPE_GCC8_UNINSTRUMENTED]


TAR_EXTRACT = 'tar xf {}'
# -o -- force overwriting existing files
ZIP_EXTRACT = 'unzip -q -o {}'
ARCHIVE_TYPES = {
    '.tar.bz2': TAR_EXTRACT,
    '.tar.gz': TAR_EXTRACT,
    '.tar.xz': TAR_EXTRACT,
    '.tgz': TAR_EXTRACT,
    '.zip': ZIP_EXTRACT,
}


def fatal(message):
    log(message)
    traceback.print_stack()
    sys.exit(1)


def log(message=""):
    sys.stderr.write(message + "\n")


def colored_log(color, message):
    sys.stderr.write(color + message + NO_COLOR + "\n")


def print_line_with_colored_prefix(prefix, line):
    log("{}[{}] {}{}".format(CYAN_COLOR, prefix, NO_COLOR, line.rstrip()))


def log_output(prefix, args, log_cmd=True):
    try:
        print_line_with_colored_prefix(
            prefix, "Running command: {} (current directory: {})".format(
                args, os.getcwd()))
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in iter(process.stdout.readline, ''):
            print_line_with_colored_prefix(prefix, line)

        process.stdout.close()
        exit_code = process.wait()
        if exit_code:
            fatal("Execution failed with code: {}".format(exit_code))
    except OSError, err:
        log("Error when trying to execute command: " + str(args))
        log("PATH is: {}".format(os.getenv("PATH")))
        raise


def unset_if_set(name):
    if name in os.environ:
        log('Unsetting {} for third-party build (was set to "{}").'.format(name, os.environ[name]))
        del os.environ[name]


def log_separator():
    log("")
    log(SEPARATOR)
    log("")


def heading(title):
    log("")
    log(SEPARATOR)
    log(title)
    log(SEPARATOR)
    log("")


def is_mac():
    return platform.system().lower() == 'darwin'


def is_linux():
    return platform.system().lower() == 'linux'


def is_jenkins_user():
    return os.environ['USER'] == "jenkins"


def is_jenkins():
    return 'BUILD_ID' in os.environ and 'JOB_NAME' in os.environ and is_jenkins_user()


def is_ubuntu():
    etc_issue_path = '/etc/issue'
    if not os.path.exists(etc_issue_path):
        return False
    with open(etc_issue_path) as etc_issue_file:
        contents = etc_issue_file.read()
        return contents.startswith('Ubuntu')


def remove_path(path):
    if not os.path.exists(path):
        return
    if os.path.islink(path):
        os.unlink(path)
    elif os.path.isdir(path):
        shutil.rmtree(path)
    else:
        os.remove(path)


def mkdir_if_missing(path):
    if os.path.exists(path):
        if not os.path.isdir(path):
            fatal("Trying to create dir {}, but file with the same path already exists"
                  .format(path))
        return
    os.makedirs(path)


def make_archive_name(name, version, download_url):
    if download_url is None:
        return '{}-{}{}'.format(name, version, '.tar.gz')
    for ext in ARCHIVE_TYPES:
        if download_url.endswith(ext):
          return '{}-{}{}'.format(name, version, ext)
    return None


def which(exe):
    return subprocess.check_output(['which', exe]).rstrip()


def import_submodules(package, recursive=True):
    if isinstance(package, str):
        package = importlib.import_module(package)
    results = {}
    for loader, name, is_pkg in pkgutil.walk_packages(package.__path__):
        full_name = package.__name__ + '.' + name
        results[full_name] = importlib.import_module(full_name)
        if recursive and is_pkg:
            results.update(import_submodules(full_name))
    return results


class Dependency(object):
    def __init__(self, name, version, url_pattern, build_group):
        self.name = name
        self.version = version
        self.dir = '{}-{}'.format(name, version)
        self.underscored_version = version.replace('.', '_')
        if url_pattern is not None:
            self.download_url = url_pattern.format(version, self.underscored_version)
        else:
            self.download_url = None
        self.build_group = build_group
        self.archive_name = make_archive_name(name, version, self.download_url)
        self.patch_version = 0

    def should_build(self, builder):
        return True


class ExtraDownload(object):
    def __init__(self, name, version, url_pattern, dir, post_exec=None):
        self.name = name
        self.version = version
        self.download_url = url_pattern.format(version)
        self.archive_name = make_archive_name(name, version, self.download_url)
        self.dir = dir
        if post_exec is not None:
            self.post_exec = post_exec

class PushDir:
    def __init__(self, dir):
        self.dir = dir
        self.prev = None

    def __enter__(self):
        self.prev = os.getcwd()
        os.chdir(self.dir)

    def __exit__(self, type, value, traceback):
        os.chdir(self.prev)


def get_openssl_dir():
    """
    Returns the custom OpenSSL installation directory to use or None.
    """
    if is_mac():
        custom_homebrew_dir = os.getenv('YB_CUSTOM_HOMEBREW_DIR')
        if custom_homebrew_dir:
            opt_path = os.path.join(custom_homebrew_dir, 'opt')
        else:
            opt_path = '/usr/local/opt'
        openssl_dir = os.path.join(opt_path, 'openssl')
        if not os.path.exists(openssl_dir):
            raise IOError("Directory does not exist: " + openssl_dir)
        return openssl_dir
    else:
        linuxbrew_dir = os.getenv('YB_LINUXBREW_DIR')
        if linuxbrew_dir:
            opt_path = os.path.join(linuxbrew_dir, 'opt')
        else:
            opt_path = '/usr/local/opt'
        openssl_dir = os.path.join(opt_path, 'openssl')
        if not os.path.exists(openssl_dir):
            raise IOError("Directory does not exist: " + openssl_dir)
        return openssl_dir


def get_openssl_related_cmake_args():
    """
    Returns a list of CMake arguments to use to pick up the version of OpenSSL that we should be
    using. Returns an empty list if the default OpenSSL installation should be used.
    """
    openssl_dir = get_openssl_dir()
    if openssl_dir:
        openssl_options = ['-DOPENSSL_ROOT_DIR=' + openssl_dir]
        if os.getenv('YB_CUSTOM_HOMEBREW_DIR'):
            openssl_crypto_library = os.path.join(openssl_dir, 'lib', 'libcrypto.dylib')
            openssl_ssl_library = os.path.join(openssl_dir, 'lib', 'libssl.dylib')
            openssl_options += [
                '-DOPENSSL_CRYPTO_LIBRARY=' + openssl_crypto_library,
                '-DOPENSSL_SSL_LIBRARY=' + openssl_ssl_library,
                '-DOPENSSL_LIBRARIES=%s;%s' % (openssl_crypto_library, openssl_ssl_library)
            ]
        if os.getenv('YB_LINUXBREW_DIR'):
            openssl_crypto_library = os.path.join(openssl_dir, 'lib', 'libcrypto.so')
            openssl_ssl_library = os.path.join(openssl_dir, 'lib', 'libssl.so')
            openssl_options += [
                '-DOPENSSL_CRYPTO_LIBRARY=' + openssl_crypto_library,
                '-DOPENSSL_SSL_LIBRARY=' + openssl_ssl_library,
                '-DOPENSSL_LIBRARIES=%s;%s' % (openssl_crypto_library, openssl_ssl_library)
            ]
        return openssl_options

    return []
