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
  --all
    Clean all third-party dependency build artifacts. This is done using a "git clean" command.
EOT
}

delete_dir() {
  expect_num_args 1 "$@"
  local dir_path=$1
  if [[ -d $dir_path ]]; then
    ( set -x; rm -rf "$dir_path" )
  else
    log "'$dir_path' is not a directory or does not exist"
  fi
}

delete_file() {
  expect_num_args 1 "$@"
  local file_path=$1
  if [[ -d $file_path ]]; then
    ( set -x; rm -f "$file_path" )
  else
    log "'$file_path' is not a file or does not exist"
  fi
}

dependency_names_to_clean=()
if [[ $# -eq 0 ]]; then
  show_usage >&2
  exit 1
fi

clean_all=false
while [[ $# -gt 0 ]]; do
  case $1 in
    -h|--help)
      show_usage >&2
      exit 1
    ;;
    --all)
      clean_all=true
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
    log "Error: invalid third-party dependency name: '$dep_name'"
  fi
done

if "$found_errors"; then
  log "Errors found, not cleaning anything."
  exit 1
fi

for dep_name in "${dependency_names_to_clean[@]}"; do
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
    delete_file "$top_build_dir/.build-stamp-$dep_name"

    if [[ -n $src_dir_name ]]; then
      delete_dir "$top_build_dir/$src_dir_name"
    fi
  done
done
