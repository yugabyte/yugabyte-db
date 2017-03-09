#!/usr/bin/env python

# Copyright (c) YugaByte, Inc.

# This script makes a few changes to the rpath field (dynamic library search path) of executables
# and shared libraries in the thirdparty directory.  We go through all executables and libraries in
# installation directories in the thirdparty directory, and fix rpath/runpath entries so that they
# are relative. This has to be done before third-party dependencies are packaged. Also, there is an
# ad-hoc fix for glog's rpath so that it can find gflags. We also run this script after downloading
# pre-built third-party dependencies onto a Linux dev server / Jenkins slave as well, because the
# third-party dependencies will likely be in a directory different than they were compiled in.
# However, that should only be necessary if there are any changes to this script after the pre-built
# third-party package was built.

import argparse
import logging
import os
import pipes
import re
import subprocess
import sys

READELF_RPATH_RE = re.compile(r'Library (?:rpath|runpath): \[(.+)\]')

# We have to update paths to Linuxbrew libraries dependent on the home directory. We are using a
# special location for a Linuxbrew installation used for building the YB codebase.
LINUXBREW_PATH_RE = re.compile(r'/home/[^/]+/([.]linuxbrew-yb-build/.*)$')

HOME_DIR = os.path.expanduser('~')


def run_ldd(elf_file_path, report_missing_libs=False):
    """
    Run ldd on the given ELF file to check if all libraries are found.

    @param elf_file_path: ELF file (executable/library) path to run ldd on.
    @param report_missing_libs: whether or not missing libraries referenced by the binary should be
                                logged.
    @return: a tuple of success/failure flag and a list of missing libraries.
    """
    ldd_subprocess = subprocess.Popen(
        ['/usr/bin/ldd', elf_file_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    ldd_stdout, ldd_stderr = ldd_subprocess.communicate()
    if ldd_subprocess.returncode != 0:
        logging.warn("ldd returned an exit code %d for '%s', stderr:\n%s" % (
            ldd_subprocess.returncode, elf_file_path, ldd_stderr))
        return (False, [])

    missing_libs = []
    for ldd_output_line in ldd_stdout.split("\n"):
        tokens = ldd_output_line.split()
        if len(tokens) >= 4 and tokens[1:4] == ['=>', 'not', 'found']:
            missing_lib_name = tokens[0]
            # We ignore c-index-test (part of clang) as it frequently comes for some reason but is
            # not a real issue.
            if os.path.basename(elf_file_path) != 'c-index-test':
                if report_missing_libs:
                    logging.warn("Library not found for '%s': %s", elf_file_path, missing_lib_name)
                missing_libs.append(missing_lib_name)
    if missing_libs:
        return (False, missing_libs)
    return (True, [])


def add_if_absent(items, new_item):
    """
    Add the given item to the given list if it does not exist there yet.
    @param items: a list to modify
    @param new_item: new item to add if it is not present in items
    """
    if new_item not in items:
        items.append(new_item)


def main():
    """
    Main entry point. Returns True on success.
    """

    parser = argparse.ArgumentParser(
        description='Fix rpath for YugaByte and third-party binaries.')
    parser.add_argument('--verbose', dest='verbose', action='store_true',
                        help='Verbose output')
    args = parser.parse_args()

    log_level = logging.INFO
    if args.verbose:
        log_level = logging.DEBUG
    logging.basicConfig(
        level=log_level,
        format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

    is_success = True
    thirdparty_dir = os.path.realpath(
        os.path.join(os.path.dirname(os.path.realpath(__file__)), '..', 'thirdparty'))

    elf_files = []

    if sys.platform == 'darwin':
        logging.info("fix_rpath.py does not do anything on Mac OS X")
        return True

    logging.info("Fixing rpath/runpath for the third-party binaries")

    if os.path.isdir(os.path.join(thirdparty_dir, 'installed', 'common')):
        logging.info("Using new directory hierarchy layout under thirdparty/installed")
        # This is an indication that we are using a new-style directory layout in
        # thirdparty/installed.
        prefix_dirs = [
            os.path.join(thirdparty_dir, 'installed', prefix_rel_dir)
            for prefix_rel_dir in ['common', 'tsan', 'uninstrumented']
            ]
    else:
        # Old-style directory structure, to be removed once migration is complete.
        logging.info("Using old directory hierarchy layout under thirdparty/installed*")
        prefix_dirs = [
            os.path.join(thirdparty_dir, prefix_rel_dir)
            for prefix_rel_dir in ['installed', 'installed-deps', 'installed-deps-tsan']
            ]

    # Prefer patchelf to chrpath as it is more flexible and can in fact increase the length of rpath
    # if there is enough space. This could happen if rpath used to be longer but was reduced by a
    # previous patchelf / chrpath command.
    change_rpath_cmd = 'chrpath -r'
    for patchelf_path in ['/usr/bin/patchelf',
                          os.path.join(HOME_DIR, '.linuxbrew-yb-build', 'bin', 'patchelf')]:
        if os.path.isfile(patchelf_path):
            change_rpath_cmd = patchelf_path + ' --set-rpath'
    logging.info("Command for updating rpath: {}".format(change_rpath_cmd))

    num_binaries_no_rpath_change = 0
    num_binaries_updated_rpath = 0

    for prefix_dir in prefix_dirs:
        for file_dir, dirs, file_names in os.walk(prefix_dir):
            for file_name in file_names:
                if file_name.endswith('_patchelf_tmp'):
                    continue
                binary_path = os.path.join(file_dir, file_name)
                if ((os.access(binary_path, os.X_OK) or
                     file_name.endswith('.so') or
                     '.so.' in file_name)
                        and not os.path.islink(binary_path)
                        and not file_name.endswith('.sh')
                        and not file_name.endswith('.py')
                        and not file_name.endswith('.la')):

                    # Invoke readelf to read the current rpath.
                    readelf_subprocess = subprocess.Popen(
                        ['/usr/bin/readelf', '-d', binary_path],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE)
                    readelf_stdout, readelf_stderr = readelf_subprocess.communicate()

                    if 'Not an ELF file' in readelf_stderr:
                        logging.warn("Not an ELF file: '%s', skipping" % binary_path)
                        continue

                    if readelf_subprocess.returncode != 0:
                        logging.warn("readelf returned exit code %d for '%s', stderr:\n%s" % (
                            readelf_subprocess.returncode, binary_path, readelf_stderr))
                        return False

                    elf_files.append(binary_path)

                    # Collect and process all rpath entries and resolve $ORIGIN (the directory of
                    # the executable or binary itself). We will put $ORIGIN back later.

                    original_rpath_entries = []  # Entries with $ORIGIN left as is
                    original_real_rpath_entries = []  # Entries with $ORIGIN substituted

                    for readelf_out_line in readelf_stdout.split('\n'):
                        matcher = READELF_RPATH_RE.search(readelf_out_line)
                        if matcher:
                            for rpath_entry in matcher.groups()[0].split(':'):
                                # Remove duplicate entries but preserve order.
                                add_if_absent(original_rpath_entries, rpath_entry)
                                rpath_entry = rpath_entry.replace('$ORIGIN', file_dir)
                                rpath_entry = rpath_entry.replace('${ORIGIN}', file_dir)
                                # Ignore a special kind of rpath entry that we add just to increase
                                # the number of bytes reserved for rpath.
                                if not rpath_entry.startswith(
                                        '/tmp/making_sure_we_have_enough_room_to_set_rpath_later'):
                                    add_if_absent(original_real_rpath_entries,
                                                  os.path.realpath(rpath_entry))

                    new_relative_rpath_entries = []
                    logging.debug("Original rpath from '%s': %s" % (
                        binary_path, ':'.join(original_rpath_entries)))

                    # A special case: the glog build ends up missing the rpath entry for gflags.
                    if file_name.startswith('libglog.'):
                        add_if_absent(original_real_rpath_entries,
                                      os.path.realpath(os.path.dirname(binary_path)))

                    if not original_real_rpath_entries:
                        logging.debug("No rpath entries in '%s', skipping", binary_path)
                        continue

                    new_real_rpath_entries = []
                    for rpath_entry in original_real_rpath_entries:
                        real_rpath_entry = os.path.realpath(rpath_entry)
                        linuxbrew_rpath_match = LINUXBREW_PATH_RE.match(real_rpath_entry)
                        if linuxbrew_rpath_match:
                            # This is a Linuxbrew directory relative to the home directory of the
                            # user that built the third-party package. We need to substitute the
                            # current user's home directory into that instead.
                            new_rpath_entry = os.path.join(HOME_DIR,
                                                           linuxbrew_rpath_match.group(1))
                        else:
                            rel_path = os.path.relpath(real_rpath_entry, os.path.realpath(file_dir))

                            # Normalize the new rpath entry by making it relative to $ORIGIN. This
                            # is only necessary for third-party libraries that may need to be moved
                            # from place to place.
                            if rel_path == '.':
                                new_rpath_entry = '$ORIGIN'
                            else:
                                new_rpath_entry = '$ORIGIN/' + rel_path
                            if len(new_rpath_entry) > 2 and new_rpath_entry.endswith('/.'):
                                new_rpath_entry = new_rpath_entry[:-2]

                        # Remove duplicate entries but preserve order. There may be further
                        # deduplication at this point, because we may have entries that only differ
                        # in presence/absence of a trailing slash, which only get normalized here.
                        add_if_absent(new_relative_rpath_entries, new_rpath_entry)
                        add_if_absent(new_real_rpath_entries, real_rpath_entry)

                    # We have to make rpath entries relative for third-party dependencies.
                    if original_rpath_entries == new_relative_rpath_entries:
                        logging.debug("No change in rpath entries for '%s' (already relative)",
                                      binary_path)
                        num_binaries_no_rpath_change += 1
                        continue

                    new_rpath_str = ':'.join(new_relative_rpath_entries)
                    # When using patchelf, this will actually set RUNPATH as a newer replacement
                    # for RPATH, with the difference being that RUNPATH can be overwritten by
                    # LD_LIBRARY_PATH, but we're OK with it.
                    # Note: pipes.quote is a way to escape strings for inclusion in shell
                    # commands.
                    set_rpath_exit_code = os.system(
                        "%s %s %s" % (change_rpath_cmd,
                                      pipes.quote(new_rpath_str),
                                      binary_path)
                    ) >> 8
                    logging.info(
                        "Setting rpath on '%s' to '%s'" % (binary_path, new_rpath_str))
                    if set_rpath_exit_code != 0:
                        logging.warn("Could not set rpath on '%s': exit code %d" % (
                                     binary_path, set_rpath_exit_code))
                        is_success = False
                    num_binaries_updated_rpath += 1

    logging.info("Number of binaries with no rpath change: {}, updated rpath: {}".format(
        num_binaries_no_rpath_change, num_binaries_updated_rpath))

    could_resolve_all = True
    logging.info("Checking if all libraries can be resolved")
    for binary_path in elf_files:
        all_libs_found, missing_libs = run_ldd(binary_path, report_missing_libs=True)
        if not all_libs_found:
            could_resolve_all = False

    if could_resolve_all:
        logging.info("All libraries resolved successfully!")
    else:
        logging.error("Some libraries could not be resolved.")

    return is_success and could_resolve_all


if __name__ == '__main__':
    if main():
        sys.exit(0)
    else:
        logging.error("Errors happened when fixing runpath/rpath, please check the log above.")
        sys.exit(1)
