#!/usr/bin/env bash

# shellcheck source=build-support/common-test-env.sh
. "${BASH_SOURCE%/*}"/../build-support/common-test-env.sh

show_help() {
  cat >&2 <<-EOT
Usage: ${0##*/} [<options>]
Options:
  --package-path <path>
    Path to the release package to test. If not specified, the latest release package in the build
    directory is used.
  --docker-image <image>
    Docker image to use for testing. If not specified, AlmaLinux 8 is used.
EOT
}

find_package() {
  # We are OK with using ls here instead of find.
  # shellcheck disable=SC2012
  YB_PACKAGE_PATH=$( ls -t "$YB_SRC_ROOT/build/yugabyte-"*.tar.gz | head -1 )
}

YB_PACKAGE_PATH=""
docker_image=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h)
      show_help
      exit 0
    ;;
    --package-path)
      YB_PACKAGE_PATH=$2
      shift
    ;;
    --docker-image)
      docker_image=$2
      shift
    ;;
  esac
  shift
done

if [[ -z ${YB_PACKAGE_PATH} ]]; then
  find_package
  if [[ ! -f ${YB_PACKAGE_PATH} ]]; then
    log "Could not find a release package to test in $YB_SRC_ROOT/build. Attempting to build one."
    (
      set -x;
      "$YB_SRC_ROOT/yb_release" --force
    )
    find_package
    if [[ ! -f ${YB_PACKAGE_PATH} ]]; then
      fatal "Could not find or build a release package to test in $YB_SRC_ROOT/build."
    fi
  fi
  log "Automatically selected release package ${YB_PACKAGE_PATH}."
fi

if [[ ! -f ${YB_PACKAGE_PATH} ]]; then
  fatal "Package does not exist at $YB_PACKAGE_PATH"
fi

# Have to export this for the script inside Docker to see it.
export YB_PACKAGE_PATH

if [[ -z ${docker_image} ]]; then
  docker_image=almalinux:8
  log "Automatically selected Docker image ${docker_image}."
fi

# Do a quick sanity test on the release package. This verifies that we can at least start the
# cluster, which requires all RPATHs to be set correctly, either at the time the package is
# built (new approach), or by post_install.sh (legacy Linuxbrew based approach).
#
# To test this locally, build the package using yb_release
#
set +e
docker run -i \
  -e YB_PACKAGE_PATH \
  --mount "type=bind,source=${YB_SRC_ROOT}/build,target=/mnt/dir_with_package" "$docker_image" \
  bash -c '
    set -euo pipefail -x
    yum install -y libatomic
    package_name=${YB_PACKAGE_PATH##*/}
    package_path=/mnt/dir_with_package/$package_name
    set +e
    # This will be "yugabyte-a.b.c.d/" (with a trailing slash).
    dir_name_inside_archive=$(tar tf "$package_path" | head -1)
    set -e
    # Remove the trailing slash.
    dir_name_inside_archive=${dir_name_inside_archive%/}
    cd /tmp
    tar xzf "${package_path}"
    cd "${dir_name_inside_archive}"
    bin/post_install.sh
    if grep -q "CentOS Linux 7" /etc/os-release; then
      python_executable=python
    else
      dnf install -y python38 procps-ng
      python_executable=python3
    fi
    $python_executable bin/yb-ctl create
    attempt=1
    sql_cmd="create table t (k int primary key, v int);
             insert into t values (1, 2);
             select * from t;"
    set +x
    while ! ( set -x; time bin/ysqlsh -c "$sql_cmd" ); do
      echo "ysqlsh failed at attempt $attempt"
      if [[ $attempt -eq 1 ]]; then
        for log_path in $( find ~/yugabyte-data -name "*.INFO" ); do
          echo
          echo "=============================================================================="
          echo "Contents of $log_path:"
          echo "=============================================================================="
          echo
          cat "$log_path"
          echo
          echo "=============================================================================="
          echo "End of $log_path"
          echo "=============================================================================="
          echo
        done
      fi
      if [[ $attempt -ge 10 ]]; then
        echo "Giving up after $attempt attempts." >&2
        exit 1
      fi
      echo "Waiting for 5 seconds before the next attempt..."
      sleep 5
      (( attempt+=1 ))
    done
  '

exit_code=$?
set -e

if [[ $exit_code -eq 0 ]]; then
  log "Docker-based package test SUCCEEDED"
else
  log "Docker-based package test FAILED (exit code: $exit_code)"
fi

exit "$exit_code"
