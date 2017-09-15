#!/usr/bin/env bash

. "${BASH_SOURCE%/*}/thirdparty-common.sh"

cd "$YB_THIRDPARTY_DIR"

show_usage() {
  cat <<-EOT
${0##*/} -- cleans third-party builds from various subdirectories in thirdparty
If invoked with no arguments, cleans all third-party builds. Never removes downloaded third-party
archives.
Usage: ${0##*/} [<options>] [<dependency_names>]
Options:
  -h, --help
    Show usage
  --downloads, --download, -d
    Also clean downloads for the chosen dependencies. This could cause large dependencies to be
    re-downloaded, so should be used carefully.
  --all
    Clean all third-party dependency build artifacts. This is done using a "git clean" command.
EOT
}

realpath() {
  python -c "import os; import sys; print os.path.realpath(sys.argv[1])" "$@"
}

delete_dir() {
  expect_num_args 1 "$@"
  local dir_path=$( realpath "$1" )
  if [[ -d $dir_path ]]; then
      log "DELETING directory '$dir_path'"
    ( set -x; rm -rf "$dir_path" )
  else
    log "'$dir_path' is not a directory or does not exist"
  fi
}

delete_file() {
  expect_num_args 1 "$@"
  local file_glob=$1
  local file_paths=( $file_glob )
  local file_path
  for file_path in "${file_paths[@]}"; do
    file_path=$( realpath "$file_path" )
    if [[ -f $file_path ]]; then
      log "DELETING file '$file_path'"
      ( set -x; rm -f "$file_path" )
    else
      log "'$file_path' is not a file or does not exist"
    fi
  done
}

dependency_names_to_clean=()
if [[ $# -eq 0 ]]; then
  show_usage >&2
  exit 1
fi

clean_all=false
delete_downloads=false

while [[ $# -gt 0 ]]; do
  case $1 in
    -h|--help)
      show_usage >&2
      exit 1
    ;;
    --all)
      clean_all=true
    ;;
    --downloads|--download|-d)
      delete_downloads=true
    ;;
    -*)
      fatal "Invalid option: $1"
    ;;
    *)
      dependency_names_to_clean+=( "$1" )
  esac
  shift
done

if "$clean_all"; then
  set -x
  # Do not remove downloaded third-party tarballs or Vim's temporary files.
  git clean -dxf \
    --exclude download/ \
    --exclude '*.sw?'
  exit
fi

found_errors=false
for dep_name in "${dependency_names_to_clean[@]}"; do
  if [[ -z ${TP_VALID_DEP_NAME_SET[$dep_name]:-} ]]; then
    log "Possible valid dependency names:"
    (
      for possible_dep_name in "${!TP_VALID_DEP_NAME_SET[@]}"; do
        echo "  ${possible_dep_name}"
      done
    ) | sort >&2
    fatal "Error: invalid third-party dependency name: '$dep_name'"
  fi
  if [[ -z ${TP_NAME_TO_ARCHIVE_NAME[$dep_name]:-} ]]; then
    fatal "Internal error: dependency name '$dep_name' not found in TP_NAME_TO_ARCHIVE_NAME"
  fi
done

if "$found_errors"; then
  log "Errors found, not cleaning anything."
  exit 1
fi

for dep_name in "${dependency_names_to_clean[@]}"; do
  archive_name=${TP_NAME_TO_ARCHIVE_NAME[$dep_name]}
  (
    set -x
    rm -rfv $YB_THIRDPARTY_DIR/build/{common,uninstrumented,tsan}/{$dep_name,.build-stamp-$dep_name}
  )
  src_dir_path=${TP_NAME_TO_SRC_DIR[$dep_name]:-}
  src_dir_name=${src_dir_path##*/}
  if [[ -z $src_dir_path ]]; then
    log "Dependency '$dep_name' does not have well-defined source/build directories, not deleting."
  else
    delete_dir "$src_dir_path"
  fi

  for top_build_dir in $YB_THIRDPARTY_DIR/build/{common,uninstrumented,tsan}; do
    (
      cd "$top_build_dir"
      delete_file ".build-stamp-$dep_name"

      if [[ -n $src_dir_name ]]; then
        delete_dir "$src_dir_name"
        delete_dir "${src_dir_name}_static"
        delete_dir "${src_dir_name}_shared"
      fi
    )
  done

  (
    cd "$YB_THIRDPARTY_DIR"
    delete_file "download/$archive_name"
  )
done
