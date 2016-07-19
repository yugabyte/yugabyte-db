#!/usr/bin/env python

# Copyright (c) YugaByte, Inc.

# Go through all executables and libraries in installation directories in the thirdparty directory,
# and fix rpath/runpath entries so that they are relative. This has to be done before third-party
# dependencies are packaged. Also, there is an ad-hoc fix for glog's rpath so that it can find
# gflags. We also run this script after downloading pre-build third-party dependencies as well.
# Finally, there is a mode where this script can do the same on YugaByte binaries after the main
# build. That mode is triggered using the --build-root option. On Mac OS X, we simply fix some
# librocksdb-related issues. The differences between Linux and Mac OS X are caused by different
# mechanisms of dynamic library resolution in these operating systems.

import argparse
import logging
import os
import pipes
import re
import subprocess
import sys

# We treat RPATH and RUNPATH uniformly, but set RUNPATH in the end.
READELF_RPATH_RE = re.compile(r'Library (?:rpath|runpath): \[(.+)\]')
def run_ldd(elf_file_path, report_missing_libs=False):
  """
  Run ldd on the given ELF file to check if all libraries are found.
  @return a tuple of success/failure flag and a list of missing libraries
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
      if report_missing_libs:
        logging.warn("Library not found for '%s': %s", elf_file_path, missing_lib_name)
      missing_libs.append(missing_lib_name)
  if missing_libs:
    return (False, missing_libs)
  return (True, [])

def add_if_absent(items, new_item):
  if new_item not in items:
    items.append(new_item)

def get_rpath_on_mac(executable_path):
  otool_subprocess= subprocess.Popen(
    ['/usr/bin/otool', '-l', executable_path],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE)
  otool_stdout, otool_stderr = otool_subprocess.communicate()
  if otool_subprocess.returncode != 0:
    raise RuntimeError("Failed to retrieve RPATH of '{}'".format(executable_path))
  lines = [l.strip() for l in otool_stdout.strip().split('\n')]
  rpaths = []
  for i in xrange(len(lines) - 2):
    if lines[i] == 'cmd LC_RPATH' and lines[i + 2].startswith('path'):
      rpath = lines[i + 2].split()[1]
      rpaths.append(rpath)
  return rpaths

def main():
  """
  Main entry point. Returns True on success.
  """
  logging.basicConfig(
    level=logging.INFO,
    format="[" + os.path.basename(__file__) + "] %(asctime)s %(levelname)s: %(message)s")

  parser = argparse.ArgumentParser(
    description='Fix rpath for YugaByte and third-party binaries.')
  parser.add_argument('--build-root', dest='build_root', action='store',
                      help="Build root directory (e.g. build/debug-gcc)")
  args = parser.parse_args()

  is_success = True
  thirdparty_dir = os.path.abspath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'thirdparty'))

  elf_files = []

  is_mac = sys.platform == 'darwin'

  if args.build_root:
    build_root = args.build_root.strip()
    if not build_root:
      logging.error("Build root directory cannot be empty")
      return False
    if not os.path.isdir(build_root):
      logging.error("Build root directory '%s' does not exist" % build_root)
      return False
    build_root = os.path.abspath(build_root)

    if is_mac:
      logging.info("Fixing librocksdb path entries for the main build on Mac OS X")
    else:
      logging.info("Fixing rpath/runpath for the main build on Linux")
    prefix_dirs = [build_root]

    # We need to add this third-party dependencies directory containing libgflags to some binaries'
    # runpaths.
    if 'tsan' in os.path.basename(build_root).lower():
      default_deps_dir_name = 'installed-deps-tsan'
    else:
      default_deps_dir_name = 'installed-deps'
    default_deps_lib_dir = os.path.join(thirdparty_dir, default_deps_dir_name, 'lib')

    is_main_build = True
    is_thirdparty = False
  else:
    logging.info("Fixing rpath/runpath for the third-party binaries")
    prefix_dirs = [
      os.path.join(thirdparty_dir, prefix_rel_dir)
      for prefix_rel_dir in ['installed', 'installed-deps', 'installed-deps-tsan']
    ]
    is_main_build = False
    is_thirdparty = True


  if is_mac and is_thirdparty:
    logging.info("Nothing to do for third-party build rpath fix-up on Mac OS X because we never " +
                 "package/move the thirdparty directory on this platform. If you want to " +
                 "fix rpath for YugaByte binaries/libraries, please specify --build-root.")
    return True

  if is_mac:
    librocksdb_dylib_name = 'librocksdb_debug.4.6.dylib'
    librocksdb_dylib_abs_path = os.path.join(build_root, 'rocksdb-build', librocksdb_dylib_name)
  else:
    if os.path.isfile('/usr/bin/patchelf'):
      linux_change_rpath_cmd = 'patchelf --set-rpath'
    else:
      linux_change_rpath_cmd = 'chrpath -r'


  for prefix_dir in prefix_dirs:
    for file_dir, dirs, file_names in os.walk(prefix_dir):
      for file_name in file_names:
        if file_name.endswith('_patchelf_tmp'):
          continue
        # TODO: rename this to something that does not contain ELF (Mac OS X binary format is
        # different).
        elf_file_path = os.path.join(file_dir, file_name)
        if (os.access(elf_file_path, os.X_OK) or
            file_name.endswith('.so') or
            '.so.' in file_name) \
            and not os.path.islink(elf_file_path) \
            and not file_name.endswith('.sh') \
            and not file_name.endswith('.py') \
            and not file_name.endswith('.la'):

          # ---------------------------------------------------------------------------------------
          # Mac OS X
          # ---------------------------------------------------------------------------------------

          if is_mac:
            # For Mac OS X builds we only do a minimal fix for the librocksdb path.
            install_name_tool_exit_code = os.system(
              'install_name_tool -change %s %s %s' % (
                pipes.quote(librocksdb_dylib_name),
                pipes.quote(librocksdb_dylib_abs_path),
                pipes.quote(elf_file_path))
            ) >> 8
            if install_name_tool_exit_code != 0:
              logging.error("Failed to update librocksdb path for '%s' using install_name_tool" %
                            elf_flie_path)
              return False

            rpath_entries = get_rpath_on_mac(elf_file_path)
            if default_deps_lib_dir not in rpath_entries:
              logging.info("Adding {} to {}'s rpath".format(default_deps_lib_dir, elf_file_path))
              if os.system(
                'install_name_tool -add_rpath {} {}'.format(
                  pipes.quote(default_deps_lib_dir),
                  pipes.quote(elf_file_path)
                )
              ) >> 8 != 0:
                raise RuntimeError("Failed to add rpath to '{}'".format(elf_file_path))

            continue

          # ---------------------------------------------------------------------------------------
          # Linux
          # ---------------------------------------------------------------------------------------

          # Invoke readelf to read the current rpath.
          readelf_subprocess = subprocess.Popen(
            ['/usr/bin/readelf', '-d', elf_file_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
          readelf_stdout, readelf_stderr = readelf_subprocess.communicate()

          if 'Not an ELF file' in readelf_stderr:
            logging.warn("Not an ELF file: '%s', skipping" % elf_file_path)
            continue

          if readelf_subprocess.returncode != 0:
            logging.warn("readelf returned exit code %d for '%s', stderr:\n%s" % (
              readelf_subprocess.returncode, elf_file_path, readelf_stderr))
            return False

          elf_files.append(elf_file_path)

          # Collect and process all rpath entries and resolve $ORIGIN (the directory of the
          # executable or binary itself). We will put $ORIGIN back later.

          original_rpath_entries = []  # Entries with $ORIGIN left as is
          rpath_entries = []           # Entries with $ORIGIN substituted

          for readelf_out_line in readelf_stdout.split('\n'):
            matcher = READELF_RPATH_RE.search(readelf_out_line)
            if matcher:
              for rpath_entry in matcher.groups()[0].split(':'):
                # Remove duplicate entries but preserve order.
                add_if_absent(original_rpath_entries, rpath_entry)
                rpath_entry = rpath_entry.replace('$ORIGIN', file_dir)
                rpath_entry = rpath_entry.replace('${ORIGIN}', file_dir)
                add_if_absent(rpath_entries, rpath_entry)

          new_rpath_entries = []
          logging.info("Original rpath from '%s': %s" % (
            elf_file_path, ':'.join(original_rpath_entries)))

          # A special case: the glog build ends up missing the rpath entry for gflags.
          if not is_main_build and file_name.startswith('libglog.'):
            add_if_absent(rpath_entries, os.path.dirname(elf_file_path))

          # A number of executables are also not being able to find libgflags.
          if is_main_build:
            all_libs_found, missing_libs = run_ldd(elf_file_path, report_missing_libs=False)
            if not all_libs_found and any(
                missing_lib.startswith('libgflags.') for missing_lib in missing_libs):
              logging.info("'%s' is not being able to find libgflags, adding deps runpath" %
                           elf_file_path)
              add_if_absent(rpath_entries, default_deps_lib_dir)

          if not rpath_entries:
            logging.info("No rpath entries in '%s', skipping", elf_file_path)
            continue

          for rpath_entry in rpath_entries:
            rel_path = os.path.relpath(os.path.abspath(rpath_entry), os.path.abspath(file_dir))

            # Normalize the new rpath entry.
            if rel_path == '.':
              new_rpath_entry = '$ORIGIN'
            else:
              new_rpath_entry = '$ORIGIN/' + rel_path
            if len(new_rpath_entry) > 2 and new_rpath_entry.endswith('/.'):
              new_rpath_entry = new_rpath_entry[:-2]

            # Remove duplicate entries but preserve order. There may be further deduplication
            # at this point, because we may have entries that only differ in presence/absence
            # of a trailing slash, which only get normalized here.
            if new_rpath_entry not in new_rpath_entries:
              new_rpath_entries.append(new_rpath_entry)

          if original_rpath_entries == new_rpath_entries:
            logging.info("No change in rpath entries for '%s'", elf_file_path)
          else:
            new_rpath_str = ':'.join(new_rpath_entries)
            # This will actually set RUNPATH as newer replacement for RPATH, with the difference
            # being that RUNPATH can be overwritten by LD_LIBRARY_PATH, but we're OK with it.
            # Note: pipes.quote is a way to escape strings for inclusion in shell commands.
            set_rpath_exit_code = os.system(
              "%s %s %s" % (linux_change_rpath_cmd,
                            pipes.quote(new_rpath_str),
                            elf_file_path)
            ) >> 8
            logging.info("Setting rpath on '%s' to '%s'" % (elf_file_path, new_rpath_str))
            if set_rpath_exit_code != 0:
              logging.warn(
                "Could not set rpath on '%s': exit code %d" % (elf_file_path, set_rpath_exit_code))
              is_success = False

  could_resolve_all = True
  if not is_mac:
    logging.info("Checking if all libraries can be resolved")
    for elf_file_path in elf_files:
      all_libs_found, missing_libs = run_ldd(elf_file_path, report_missing_libs=True)
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
