#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

# Package third-party library build artifacts so that we don't have to build them later. This
# assumes build-thirdparty.sh has already succeeded.

# TODO: also enable "set -u" to disallow using undefined variables.
set -eo pipefail

DEFAULT_DEST_DIR=/tmp/package-thirdparty

show_help() {
  cat <<-EOT
Usage: ${0##*/} [<options>]
Options:
  -h, --help
    Show help
  -d, --dest-dir <dest_dir>
    Destination directory to create the archive in ($DEFAULT_DEST_DIR by default).
  -u, --upload
    Also upload the new package to the default location on S3
EOT
}

check_build_output_dirs_exist() {
  local d
  for d in "$@"; do
    if [ ! -d "$d" ]; then
      echo "Third-party dependency build output directory '$d' does not exist." >&2
      echo "Please make sure build-thirdparty.sh succeeds before running ${0##*/}." >&2
      echo >&2
      exit 1
    fi
  done
}

upload=false
dest_dir="$DEFAULT_DEST_DIR"
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      show_help
      exit 0
    ;;
    -d|--dest-dir)
      dest_dir="$2"
      shift
    ;;
    --upload)
      upload=true
    ;;
    *)
      show_help >&2
      echo >&2
      echo "Invalid option: $1" >&2
      exit 1
  esac
  shift
done

mkdir -p "$dest_dir"

dest_dir=$( cd "$dest_dir" && pwd )

TP_DIR=$( cd "$( dirname "$0" )" && pwd )

source "$TP_DIR"/thirdparty-packaging-common.sh
source "$TP_DIR"/vars.sh

cd "$TP_DIR"

if ! git diff --quiet .; then
  echo "There are local changes in the thirdparty directory! Refusing to proceed." >&2
  exit 1
fi

# Apart from the build directories, we also need to package the gflags directory separately
# (we use it to build RocksDB).
installed_dirs=( 
  "$PREFIX_COMMON"
  "$PREFIX_DEPS"
  "$PREFIX_DEPS_TSAN"
  "$GFLAGS_DIR/lib"
  "$GFLAGS_DIR/include"
)

check_build_output_dirs_exist "${installed_dirs[@]}"

thirdparty_dir_sha1=$( git log -n 1 --pretty=format:%H . )
if [[ ! "$thirdparty_dir_sha1" =~ ^[0-9a-f]{40}$ ]]; then
  echo "Failed to get the SHA1 of the last change to the thirdparty directory" >&2
  exit 1
fi

timestamp=$( date +%Y-%m-%dT%H_%M_%S )

dest_name="$( get_prebuilt_thirdparty_name_prefix )${timestamp}__${thirdparty_dir_sha1}__${USER}"
dest_tarball_path="$dest_dir/$dest_name.tar.gz"

if [ -f "$dest_tarball_path" ]; then
  echo "The destination file already exists: $dest_tarball_path" >&2
  exit 1
fi

echo "Creating $dest_tarball_path"

tar_append_option=""
installed_dir_rel_paths=()
archived_top_level_dirs=()
for d in "${installed_dirs[@]}"; do
  if [[ "${d#$TP_DIR/}" == "$d" ]]; then
    echo "Internal error: expected the installed third-party library directory '$d'" \
      "to be a subdirectory of '$TP_DIR'" >&2
    exit 1
  fi
  rel_archived_dir="${d#$TP_DIR/}"
  installed_dir_rel_paths+=( "$rel_archived_dir" )
  # Keep track of top-level directories we're adding to the archive,
  # e.g. glog-2.1.2/lib becomes glog-2.1.2:
  archived_top_level_dirs+=( "${rel_archived_dir%%/*}" )  
done

( set -x; tar czf $tar_append_option "$dest_tarball_path" "${installed_dir_rel_paths[@]}" )

# De-duplicate archived_top_level_dirs
IFS=$'\n'
archived_top_level_dirs=(
  $( for item in "${archived_top_level_dirs[@]}"; do echo "$item"; done | sort | uniq )
)

echo "Sanity-checking the newly created tarball"
# We also create this list with IFS set to $'\n' to handle potential spaces in file names
# (but I hope we don't get any of them there).
top_level_dirs_from_tar=$(
  tar -tzf "$dest_tarball_path" | sed 's/\/.*//g' | sort | uniq
)

unset IFS

if ! diff --ignore-space-change \
    <( for d in "${archived_top_level_dirs[@]}"; do echo "$d"; done ) \
    <( echo "$top_level_dirs_from_tar" ); then
  echo "The actual list of top-level directories in the tarball is different from" \
    "what's expected ('${archived_top_level_dirs[@]}'):" >&2
  echo "$top_level_dirs_from_tar" >&2
  exit 1
fi

echo "$dest_tarball_path contains the right top-level directories"

if "$upload"; then
  echo "Uploading to S3"
  ( set -x; s3cmd put "$dest_tarball_path" "$PREBUILT_THIRDPARTY_S3_URL/${dest_tarball_path##*/}" )
fi
