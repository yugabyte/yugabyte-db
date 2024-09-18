#!/usr/bin/env bash
#
# Copyright 2020 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

set -euo pipefail

find_python_executable() {
  PYTHON_EXECUTABLES=('python3' 'python3.6' \
    'python3.7' 'python3.8' 'python3.9' 'python' 'python2' 'python2.7')
  for py_executable in "${PYTHON_EXECUTABLES[@]}"; do
    if which "$py_executable" > /dev/null 2>&1; then
      PYTHON_EXECUTABLE="$py_executable"
      return
    fi
  done

  echo "Failed to find python executable."
  exit 1
}

SCRIPT_NAME=$(basename "$0")
USER=$(whoami)
PLATFORM_DUMP_FNAME="platform_dump.sql"
PLATFORM_DB_NAME="yugaware"
PROMETHEUS_SNAPSHOT_DIR="prometheus_snapshot"
MIGRATION_BACKUP_DIR="migration_backup"
VERSION_METADATA="version_metadata.json"
VERSION_METADATA_BACKUP="version_metadata_backup.json"
PYTHON_EXECUTABLE=""
find_python_executable
# This is the UID for nobody user which is used by the prometheus container as the default user.
NOBODY_UID=65534
# When false, we won't stop/start platform and prometheus services when executing the script
RESTART_PROCESSES=true
# When true, we will ignore the pgrestore_path and use pg_restore found on the system
USE_SYSTEM_PG=false
K8S_BACKUP_DIR="/opt/yugabyte"

set +e
# Check whether the script is being run from a VM running replicated-based Yugabyte Platform.
docker ps -a 2> /dev/null | grep yugabyte-yugaware > /dev/null 2>&1
DOCKER_CHECK="$?"

if [[ $DOCKER_CHECK -eq 0 ]]; then
  DOCKER_BASED=true
else
  DOCKER_BASED=false
fi


# Check whether the script is being run from within a Yugabyte Platform docker container.
grep -E 'kubepods|docker' /proc/1/cgroup > /dev/null 2>&1
CONTAINER_CHECK="$?"

if [[ $CONTAINER_CHECK -eq 0 ]] && [[ "$DOCKER_BASED" = false ]]; then
  INSIDE_CONTAINER=true
else
  INSIDE_CONTAINER=false
fi
set -e

# Assume the script is being run from a systemctl-based Yugabyte Platform installation otherwise.
set +u # Allow checking undefined variables, mostly for the global env kubernetes_service_host
if [[ "$DOCKER_BASED" = false ]] && [[ "$INSIDE_CONTAINER" = false ]] && [[ "$KUBERNETES_SERVICE_HOST" = "" ]]; then
  SERVICE_BASED=true
else
  SERVICE_BASED=false
fi
set -u # Disallow undefined variables

# Takes docker container and command as arguments. Executes docker cmd if docker-based or not.
docker_aware_cmd() {
  if [[ "$DOCKER_BASED" = false ]]; then
    sh -c "$2"
  else
    docker exec -i "${1}" sh -c "$2"
  fi
}

run_sudo_cmd() {
  if [[ "${USER}" = "root" ]]; then
    $1
  else
    sudo $1
  fi
}

# Query prometheus for it's data directory and set as env var
set_prometheus_data_dir() {
  prometheus_host="$1"
  prometheus_port="$2"
  data_dir="$3"
  prometheus_protocol="$4"
  if [[ "$DOCKER_BASED" = true ]]; then
    PROMETHEUS_DATA_DIR="${data_dir}/prometheusv2"
  else
    curl_cmd="curl -k \
      ${prometheus_protocol}://${prometheus_host}:${prometheus_port}/api/v1/status/flags"
    if [[ -n "${PROMETHEUS_USERNAME:-}" ]] && [[ -n "${PROMETHEUS_PASSWORD:-}" ]]; then
      curl_cmd="${curl_cmd} -u ${PROMETHEUS_USERNAME}:${PROMETHEUS_PASSWORD}"
    fi
    PROMETHEUS_DATA_DIR=$($curl_cmd | ${PYTHON_EXECUTABLE} -c \
      "import sys, json; print(json.load(sys.stdin)['data']['storage.tsdb.path'])")
  fi
  if [[ -z "$PROMETHEUS_DATA_DIR" ]]; then
    echo "Failed to find prometheus data directory"
    exit 1
  fi
}

# Modify service status if the script is being run against a service-based Yugabyte Platform
modify_service() {
  if [[ "$SERVICE_BASED" = true ]] && [[ "$RESTART_PROCESSES" = true ]]; then
    set +e
    service="$1"
    operation="$2"
    echo "Performing operation $operation on service $service"
    run_sudo_cmd "systemctl ${operation} ${service}"
    set -e
  fi
}

# Creates a Yugabyte Platform DB backup.
create_postgres_backup() {
  backup_path="$1"
  db_username="$2"
  db_host="$3"
  db_port="$4"
  verbose="$5"
  yba_installer="$6"
  pgdump_path="$7"
  pg_dump="pg_dump"
  plain_sql="$8"

  format="c"
  if [[ "${plain_sql}" = true ]]; then
      # pg_dump creates a plain-text SQL script file.
      format="p"
  fi
  # Determine pg_dump path in yba-installer cases where postgres is installed in data_dir.
  if [[ "${yba_installer}" = true ]] && \
     [[ "${pgdump_path}" != "" ]] && \
     [[ -f "${pgdump_path}" ]]; then
    pg_dump="${pgdump_path}"
  fi

  if [[ "${verbose}" = true ]]; then
    backup_cmd="${pg_dump} -h ${db_host} -p ${db_port} -U ${db_username} -F${format} -v --clean \
      ${PLATFORM_DB_NAME}"
  else
    backup_cmd="${pg_dump} -h ${db_host} -p ${db_port} -U ${db_username} -F${format} --clean \
      ${PLATFORM_DB_NAME}"
  fi
  # Run pg_dump.
  echo "Creating Yugabyte Platform DB backup ${backup_path}..."
  if [[ "${yba_installer}" = true ]]; then
    # -f flag does not work for docker based installs. Tries to dump inside postgres container but
    # we need output on the host itself.
    ybai_backup_cmd="${backup_cmd} -f ${backup_path}"
    docker_aware_cmd "postgres" "${ybai_backup_cmd}"
  else
    docker_aware_cmd "postgres" "${backup_cmd}" > "${backup_path}"
  fi
  echo "Done"
}

# Restores a Yugabyte Platform DB backup.
restore_postgres_backup() {
  backup_path="$1"
  db_username="$2"
  db_host="$3"
  db_port="$4"
  verbose="$5"
  yba_installer="$6"
  pgrestore_path="$7"
  skip_dump_check="$8"
  pg_restore="pg_restore"
  psql="psql"

  # Determine pg_restore path in yba-installer cases where postgres is installed in data_dir.
  if [[ "${yba_installer}" = true ]] && \
     [[ "${pgrestore_path}" != "" ]] && \
     [[ "${USE_SYSTEM_PG}" != true ]] && \
     [[ -f "${pgrestore_path}" ]]; then
    pg_restore=${pgrestore_path}
  fi

  if [[ "${pg_restore}" == "${pgrestore_path}" ]]; then
    psql=$(dirname "${pgrestore_path}")/psql
  fi

  if grep -iq "COPY.*customer" "${backup_path}" || [[ "${skip_dump_check}" = true ]]; then
    # Drop public schema so it is guaranteed to be a clean restore
    drop_cmd="${psql} -h ${db_host} -p ${db_port} -U ${db_username} -d ${PLATFORM_DB_NAME} \
      -c \"DROP SCHEMA IF EXISTS public CASCADE;CREATE SCHEMA public;\""
    docker_aware_cmd "postgres" "${drop_cmd}"

    if [[ "${verbose}" = true ]]; then
      restore_cmd="${pg_restore} -h ${db_host} -p ${db_port} -U ${db_username} -c --if-exists -v \
        -d ${PLATFORM_DB_NAME}"
    else
      restore_cmd="${pg_restore} -h ${db_host} -p ${db_port} -U ${db_username} -c --if-exists \
        -d ${PLATFORM_DB_NAME}"
    fi

    # Run pg_restore.
    echo "Restoring Yugabyte Platform DB backup ${backup_path}..."
    docker_aware_cmd "postgres" "${restore_cmd}" < "${backup_path}"
    echo "Done"
  else
    echo "${backup_path} potentially might be empty, skipping restore. Use --skip_dump_check to \
      proceed"
  fi
}

# Creates a DB backup of YB Platform running on YBDB.
create_ybdb_backup() {
  backup_path="$1"
  db_username="$2"
  db_host="$3"
  db_port="$4"
  verbose="$5"
  yba_installer="$6"
  ysql_dump_path="$7"
  ysql_dump="ysql_dump"


  # ybdb backup is only supported in yba-installer.
  if [[ "$yba_installer" != true ]]; then
      echo "YBA YBDB backup is only supported for yba-installer"
      return
  fi

  # If a ysql_dump path is given, use it explicitly
  if [[ "${yba_installer}" = true ]] && \
       [[ "${ysql_dump_path}" != "" ]] && \
       [[ -f "${ysql_dump_path}" ]]; then
      ysql_dump="${ysql_dump_path}"
  fi

  if [[ "${verbose}" = true ]]; then
    backup_cmd="${ysql_dump} -h ${db_host} -p ${db_port} -U ${db_username} -f ${backup_path} -v \
     --clean ${PLATFORM_DB_NAME}"
  else
    backup_cmd="${ysql_dump} -h ${db_host} -p ${db_port} -U ${db_username} -f ${backup_path} \
     --clean ${PLATFORM_DB_NAME}"
  fi
  # Run ysql_dump.
  echo "Creating YBDB Platform DB backup ${backup_path}..."

  ${backup_cmd}

  echo "Done"
}

# Restores a DB backup of YB Platform running on YBDB.
restore_ybdb_backup() {
    backup_path="$1"
    db_username="$2"
    db_host="$3"
    db_port="$4"
    verbose="$5"
    yba_installer="$6"
    ysqlsh_path="$7"
    ysqlsh="ysqlsh"

    # ybdb restore is only supported in yba-installer workflow.
      if [[ "$yba_installer" != true ]]; then
          echo "YBA YBDB restore is only supported for yba-installer"
          return
      fi

    if [[ "${yba_installer}" = true ]] && \
         [[ "${ysqlsh_path}" != "" ]] && \
         [[ -f "${ysqlsh_path}" ]]; then
        ysqlsh="${ysqlsh_path}"
    fi

    # Note that we use ysqlsh and not pg_restore to perform the restore,
    # as ysql reads plain-text SQL file to support restore from both ybdb and postgres,
    # which is necessary for postgres->ybdb migration in the future.
    if [[ "${verbose}" = true ]]; then
      restore_cmd="${ysqlsh} -h ${db_host} -p ${db_port} -U ${db_username} -d \
        ${PLATFORM_DB_NAME} -f ${backup_path}"
    else
      restore_cmd="${ysqlsh} -h ${db_host} -p ${db_port} -U ${db_username} -q -d \
        ${PLATFORM_DB_NAME} -f ${backup_path}"
    fi

    # Run restore.
    echo "Restoring Yugabyte Platform YBDB backup ${backup_path}..."
    ${restore_cmd}
    echo "Done"
}

# Deletes a Yugabyte Platform DB backup.
delete_db_backup() {
  backup_path="$1"
  echo "Deleting Yugabyte Platform DB backup ${backup_path}..."
  if [[ -f "${backup_path}" ]]; then
    cleanup "${backup_path}"
    echo "Done"
  else
    echo "${backup_path} does not exist. Cannot delete"
  fi
}

create_backup() {
  now=$(date +"%y-%m-%d-%H-%M")
  output_path="${1}"
  data_dir="${2}"
  exclude_prometheus="${3}"
  exclude_releases="${4}"
  db_username="${5}"
  db_host="${6}"
  db_port="${7}"
  verbose="${8}"
  prometheus_host="${9}"
  prometheus_port="${10}"
  k8s_namespace="${11}"
  k8s_pod="${12}"
  pgdump_path="${13}"
  plain_sql="${14}"
  ybdb="${15}"
  ysql_dump_path="${16}"
  prometheus_protocol="${17}"
  include_releases_flag="**/releases/**"
  include_uploaded_releases_flag="**/upload/release_artifacts/**"

  mkdir -p "${output_path}"

  # Perform K8s backup.
  if [[ -n "${k8s_namespace}" ]] || [[ -n "${k8s_pod}" ]]; then
    # Run backup script in container.
    verbose_flag=""
    if [[ "${verbose}" == true ]]; then
      verbose_flag="-v"
    fi
    backup_script="/opt/yugabyte/devops/bin/yb_platform_backup.sh"
    # Currently, this script does not support backup/restore of Prometheus data for K8s deployments.
    # On K8s deployments (unlike Replicated deployments) the prometheus data volume for snapshots is
    # not shared between the yugaware and prometheus containers.
    exclude_flags="--exclude_prometheus"
    if [[ "$exclude_releases" = true ]]; then
      exclude_flags="${exclude_flags} --exclude_releases"
    fi
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- /bin/bash -c \
      "${backup_script} create ${verbose_flag} ${exclude_flags} --output ${K8S_BACKUP_DIR}"
    # Determine backup archive filename.
    # Note: There is a slight race condition here. It will always use the most recent backup file.
    backup_file=$(kubectl -n "${k8s_namespace}" -c yugaware exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "cd ${K8S_BACKUP_DIR} && ls -1 backup*.tgz | tail -n 1")
    backup_file=${backup_file%$'\r'}
    # Ensure backup succeeded.
    if [[ -z "${backup_file}" ]]; then
      echo "Failed"
      return
    fi

    echo "Copying backup from container"
    # Copy backup archive from container to local machine.
    kubectl -n "${k8s_namespace}" -c yugaware cp --request-timeout="${k8s_timeout}" \
      "${k8s_pod}:${K8S_BACKUP_DIR}/${backup_file}" "${output_path}/${backup_file}"

    # Delete backup archive from container.
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "rm ${K8S_BACKUP_DIR}/backup*.tgz"
    echo "Done"
    return
  fi



  if [ "$disable_version_check" != true ]; then

    metadata_regex="**/yugaware/conf/${VERSION_METADATA}"
    metadata_dir="${data_dir}"
    target_dir="${data_dir}"
    # Hardcode container values for replicated
    if [[ "$DOCKER_BASED" = true ]]; then
      metadata_dir="/opt/yugabyte"
      target_dir="/opt/yugabyte/yugaware/data"
    fi
    if [[ "${yba_installer}" = true ]]; then
      version=$(basename $(realpath ${data_dir}/software/active))
      metadata_regex="**/${version}/**/yugaware/conf/${VERSION_METADATA}"
    fi
    version_path=$(docker_aware_cmd "yugaware" "find ${metadata_dir} -wholename ${metadata_regex}")

    command="cp ${version_path} ${target_dir}/${VERSION_METADATA_BACKUP}"
    docker_aware_cmd "yugaware" "${command}"
  fi

  if [[ "$exclude_releases" = true ]]; then
    include_releases_flag=""
    include_uploaded_releases_flag=""
  fi

  modify_service yb-platform stop

  tar_name="${output_path}/backup_${now}.tar"
  tgz_name="${output_path}/backup_${now}.tgz"
  db_backup_path="${data_dir}/${PLATFORM_DUMP_FNAME}"
  trap 'delete_db_backup ${db_backup_path}' RETURN
  if [[ "$ybdb" = true ]]; then
    create_ybdb_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
                             "${verbose}" "${yba_installer}" "${ysql_dump_path}"
  else
    create_postgres_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
                         "${verbose}" "${yba_installer}" "${pgdump_path}" "${plain_sql}"
  fi

  TAR_OPTIONS="-r"
  if [[ "${verbose}" = true ]]; then
     TAR_OPTIONS+="v"
  fi
  TAR_OPTIONS+="f ${tar_name}"
  FIND_OPTIONS=( . \\\( -path  \'**/data/certs/**\' )
  FIND_OPTIONS+=( $(printf " -o -path '%s'"  "**/data/keys/**" "**/data/provision/**" \
              "**/data/licenses/**"  "**/data/yb-platform/keys/**" "**/data/yb-platform/certs/**" \
              "**/swamper_rules/**" "**/swamper_targets/**" "**/prometheus/rules/**"  \
              "**/prometheus/targets/**" "**/data/yb-platform/node-agent/certs/**" \
              "**/data/node-agent/certs/**" "**/provision/**/provision_instance.py" \
              "**/${PLATFORM_DUMP_FNAME}" "**/${VERSION_METADATA_BACKUP}" \
              "${include_releases_flag}" "${include_uploaded_releases_flag}") )

  # Backup prometheus data.
  if [[ "$exclude_prometheus" = false ]]; then
    trap 'run_sudo_cmd "rm -rf ${data_dir}/${PROMETHEUS_SNAPSHOT_DIR}"' RETURN
    echo "Creating prometheus snapshot..."
    set_prometheus_data_dir "${prometheus_host}" "${prometheus_port}" "${data_dir}" \
      "${prometheus_protocol}"
    snapshot_cmd="curl -k -X POST \
      ${prometheus_protocol}://${prometheus_host}:${prometheus_port}/api/v1/admin/tsdb/snapshot"
    if [[ -n "${PROMETHEUS_USERNAME:-}" ]] && [[ -n "${PROMETHEUS_PASSWORD:-}" ]]; then
      snapshot_cmd="${snapshot_cmd} -u ${PROMETHEUS_USERNAME}:${PROMETHEUS_PASSWORD}"
    fi
    snapshot_dir=$( $snapshot_cmd | ${PYTHON_EXECUTABLE} -c \
      "import sys, json; print(json.load(sys.stdin)['data']['name'])")
    mkdir -p "$data_dir/$PROMETHEUS_SNAPSHOT_DIR"
    run_sudo_cmd "cp -aR ${PROMETHEUS_DATA_DIR}/snapshots/${snapshot_dir} \
    ${data_dir}/${PROMETHEUS_SNAPSHOT_DIR}"
    run_sudo_cmd "rm -rf ${PROMETHEUS_DATA_DIR}/snapshots/${snapshot_dir}"
  FIND_OPTIONS+=( -o -path "**/${PROMETHEUS_SNAPSHOT_DIR}/**" )
  fi
  # Close out paths in FIND_OPTIONS with a close-paren, and  add -exec
  FIND_OPTIONS+=( \\\) -exec tar $TAR_OPTIONS \{} + )
  echo "Creating platform backup package..."
  cd ${data_dir}

  eval find -L ${FIND_OPTIONS[@]}

  gzip -9 < ${tar_name} > ${tgz_name}
  cleanup "${tar_name}"
  delete_db_backup "${db_backup_path}"

  # Delete the version metadata backup if we had created it earlier
  docker_aware_cmd "yugaware" "rm -f ${data_dir}/${VERSION_METADATA_BACKUP}"

  echo "Finished creating backup ${tgz_name}"
  modify_service yb-platform restart
}

restore_backup() {
  input_path="${1}"
  destination="${2}"
  db_host="${3}"
  db_port="${4}"
  db_username="${5}"
  verbose="${6}"
  prometheus_host="${7}"
  prometheus_port="${8}"
  data_dir="${9}"
  k8s_namespace="${10}"
  k8s_pod="${11}"
  disable_version_check="${12}"
  pgrestore_path="${13}"
  ybdb="${14}"
  ysqlsh_path="${15}"
  ybai_data_dir="${16}"
  skip_old_files="${17}"
  skip_dump_check="${18}"
  prometheus_protocol="${19}"
  prometheus_dir_regex="\.\/${PROMETHEUS_SNAPSHOT_DIR}\/[[:digit:]]{8}T[[:digit:]]{6}Z-[[:alnum:]]{16}\/$"

  # Perform K8s restore.
  if [[ -n "${k8s_namespace}" ]] || [[ -n "${k8s_pod}" ]]; then

    # Copy backup archive to container.
    echo "Copying backup to container"
    kubectl -n "${k8s_namespace}" -c yugaware cp --request-timeout="${k8s_timeout}" \
      "${input_path}" "${k8s_pod}:${K8S_BACKUP_DIR}"
    echo "Done"

    # Determine backup archive filename.
    # Note: There is a slight race condition here. It will always use the most recent backup file.
    backup_file=$(kubectl -n "${k8s_namespace}" -c yugaware exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "cd ${K8S_BACKUP_DIR} && ls -1 backup*.tgz | tail -n 1")
    backup_file=${backup_file%$'\r'}
    # Run restore script in container.
    verbose_flag=""
    if [[ "${verbose}" == true ]]; then
      verbose_flag="-v"
    fi
    backup_script="/opt/yugabyte/devops/bin/yb_platform_backup.sh"

    # Skip old files as script was called outside of container so may lack permissions to overwrite
    if [ "$disable_version_check" != true ]; then
      kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- /bin/bash -c \
        "${backup_script} restore ${verbose_flag} --input ${K8S_BACKUP_DIR}/${backup_file} \
        --skip_old_files"
    else
      kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- /bin/bash -c \
        "${backup_script} restore ${verbose_flag} --input ${K8S_BACKUP_DIR}/${backup_file} \
        --disable_version_check --skip_old_files"
    fi

    # Delete backup archive from container.
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "rm ${K8S_BACKUP_DIR}/backup*.tgz"
    return
  fi

  if [ "$disable_version_check" != true ]; then

    current_metadata_path=""

    if [ -f "../../src/main/resources/${VERSION_METADATA}" ]; then

        current_metadata_path="../../src/main/resources/${VERSION_METADATA}"

    else

        metadata_regex="**/yugaware/conf/${VERSION_METADATA}"
        if [[ "${yba_installer}" = true ]]; then
          version=$(basename $(realpath ${data_dir}/software/active))
          metadata_regex="**/${version}/**/yugaware/conf/${VERSION_METADATA}"
        fi
        # Ignore errors in case of directories where we don't have permissions
        set +e
        current_metadata_path=$(find ${destination} -wholename ${metadata_regex})
        set -e

        # At least keep some default as a worst case.
        if [ ! -f ${current_metadata_path} ] || [ -z ${current_metadata_path} ]; then
          current_metadata_path="${data_dir}/yugaware/conf/${VERSION_METADATA}"
        fi

    fi

    command="cat ${current_metadata_path}"

    version_cmd='import json, sys; print(json.load(sys.stdin)["version_number"])'
    build_cmd='import json, sys; print(json.load(sys.stdin)["build_number"])'

    version=$(docker_aware_cmd "yugaware" "${command}" | ${PYTHON_EXECUTABLE} -c "${version_cmd}")
    build=$(docker_aware_cmd "yugaware" "${command}" | ${PYTHON_EXECUTABLE} -c "${build_cmd}")

    curr_platform_version=${version}-${build}

    backup_metadata_path=$(tar -tzf ${input_path} | grep ${VERSION_METADATA_BACKUP} | head -1)
    if [[ "${backup_metadata_path}" == "" ]]; then
      echo "cannot perform version check on backup ${input_path}, no ${VERSION_METADATA_BACKUP}
      found. Please run restore with --disable_version_check or take a new backup with \
      ${VERSION_METADATA_BACKUP}"
      exit 1
    fi
    tar -xzf ${input_path} -C ${destination} ${backup_metadata_path}
    set +e
    backup_metadata_path=$(find ${destination} -name ${VERSION_METADATA_BACKUP} | head -1)
    set -e
    if [ ! -f ${backup_metadata_path} ] || [ -z ${backup_metadata_path} ]; then
      echo "could not find untarred ${VERSION_METADATA_BACKUP}"
      exit 1
    fi
    # The version_metadata.json file is always present in a release package, and it would have
    # been stored during create_backup(), so we don't need to check if the file exists before
    # restoring it from the restore path.
    backup_yba_version=$(cat "${backup_metadata_path}" | ${PYTHON_EXECUTABLE} -c "${version_cmd}")
    backup_yba_build=$(cat "${backup_metadata_path}" | ${PYTHON_EXECUTABLE} -c "${build_cmd}")
    back_plat_version=${backup_yba_version}-${backup_yba_build}

    # Delete the backup metadata path after using it
    rm ${backup_metadata_path}

    if [ ${curr_platform_version} != ${back_plat_version} ]
    then
      echo "Your backups were created on a platform of version ${back_plat_version}, and you are
      attempting to restore these backups on a platform of version ${curr_platform_version},
      which is a mismatch. Please restore your platform instance exactly back to
      ${back_plat_version} to proceed, or override this check by running the script with the
      command line argument --disable_version_check true"
      exit 1
    fi
  fi

  modify_service yb-platform stop

  untar_dir="${destination}"
  tar_cmd="tar -xzf"
  if [[ "${verbose}" = true ]]; then
    tar_cmd="tar -xzvf"
  fi
  if [[ "${migration}" = true ]]; then
    untar_dir="${destination}"/"${MIGRATION_BACKUP_DIR}"
    rm -rf "${untar_dir}"
    mkdir -p "${untar_dir}"
    $tar_cmd "${input_path}" --directory "${untar_dir}"

    # Copy over releases. Need to ignore node-agent/ybc releases
    set +e
    releasesdir=$(find "${untar_dir}" -name "releases" -type d | \
                  grep -v "ybc" | grep -v "node-agent")
    set -e
    if [[ "$releasesdir" != "" ]] && [[ -d "$releasesdir" ]]; then
      cp -R "$releasesdir" "$ybai_data_dir"
    fi
    # Node-agent/ybc foldes can be copied entirely into
    # Copy releases, ybc, certs, keys, over
    # xcerts/keys/licenses can all go directly into data directory
    BACKUP_DIRS=('*ybc' '*data/certs' '*data/keys' '*data/licenses' '*node-agent' \
      '*upload/release_artifacts')
    for d in "${BACKUP_DIRS[@]}"
    do
      set +e
      found_dir=$(find "${untar_dir}" -path "$d" -type d)
      set -e
      if [[ "$found_dir" != "" ]] && [[ -d "$found_dir" ]]; then
        cp -R "$found_dir" "$ybai_data_dir"
      fi
    done
  else
    $tar_cmd "${input_path}" --directory "${destination}" "${skip_old_files}"
  fi

  db_backup_path="${untar_dir}"/"${PLATFORM_DUMP_FNAME}"
  trap 'delete_db_backup ${db_backup_path}' RETURN
  if [[ "${ybdb}" = true ]]; then
    restore_ybdb_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
      "${verbose}" "${yba_installer}" "${ysqlsh_path}"
  else
    # do we need set +e?
    restore_postgres_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
      "${verbose}" "${yba_installer}" "${pgrestore_path}" "${skip_dump_check}"
  fi

  # Restore prometheus swamper targets on migration always
  if [[ "${yba_installer}" = true ]] && [[ "${migration}" = true ]]; then
    set +e
    backup_targets=$(find "${untar_dir}" -name swamper_targets -type d)
    set -e
    if  [[ "$backup_targets" != "" ]] && [[ -d "$backup_targets" ]]; then
      run_sudo_cmd "cp -Tr ${backup_targets} ${destination}/data/prometheus/swamper_targets"
    fi
    set +e
    backup_rules=$(find "${untar_dir}" -name swamper_rules -type d)
    set -e
    if  [[ "$backup_rules" != "" ]] && [[ -d "$backup_rules" ]]; then
      run_sudo_cmd "cp -Tr ${backup_rules} ${destination}/data/prometheus/swamper_rules"
    fi
    run_sudo_cmd "chown -R ${yba_user}:${yba_user} ${destination}/data/prometheus"
  fi
  set +e
  prom_snapshot=$(tar -tf "${input_path}" | grep -E $prometheus_dir_regex)
  set -e
  if [[ -n "$prom_snapshot" ]]; then
    echo "Restoring prometheus snapshot..."
    set_prometheus_data_dir "${prometheus_host}" "${prometheus_port}" "${data_dir}" \
      "${prometheus_protocol}"
    modify_service prometheus stop
    # Find snapshot directory in backup
    run_sudo_cmd "rm -rf ${PROMETHEUS_DATA_DIR}/*"
    run_sudo_cmd "mv ${untar_dir}/${prom_snapshot:2}* ${PROMETHEUS_DATA_DIR}"

    if [[ "${yba_installer}" = true ]]; then
      run_sudo_cmd "chown -R ${yba_user}:${yba_user} ${destination}/data/prometheus"
    elif [[ "$SERVICE_BASED" = true ]]; then
      run_sudo_cmd "chown -R ${prometheus_user}:${prometheus_user} ${PROMETHEUS_DATA_DIR}"
    else
      run_sudo_cmd "chown -R ${NOBODY_UID}:${NOBODY_UID} ${PROMETHEUS_DATA_DIR}"
    fi
    # Clean up snapshot after restore
    run_sudo_cmd "rm -rf ${untar_dir}/${PROMETHEUS_SNAPSHOT_DIR}"
    # Manually execute so postgres TRAP executes.
    modify_service prometheus restart
    if [[ "$DOCKER_BASED" = true ]]; then
      run_sudo_cmd "docker restart prometheus"
    fi
  fi
  # Create following directory if it wasn't created yet so restore will succeed.
  if [[ "${yba_installer}" = false ]]; then
    mkdir -p "${destination}/release"
  fi

  if [[ "$migration" = true ]]; then
    rm -rf "${destination}/${MIGRATION_BACKUP_DIR}"
  fi


  # Delete any extra version metadata files. These may not exist, so this is best effort
  rm -f ${data_dir}/${VERSION_METADATA_BACKUP}
  rm -f ${data_dir}/yugaware/${VERSION_METADATA}

  modify_service yb-platform restart

  echo "Finished restoring backup"
}

validate_k8s_args() {
  k8s_namespace="${1}"
  k8s_pod="${2}"
  if [[ -n "${k8s_namespace}" ]] || [[ -n "${k8s_pod}" ]]; then
    if [[ -z "${k8s_namespace}" ]] || [[ -z "${k8s_pod}" ]]; then
      echo "Error: Must specify both --k8s_namespace and --k8s_pod"
      exit 1
    fi
  fi
}

validate_prometheus_args() {
  if [[ $prometheus_protocol != "http" ]] && [[ $prometheus_protocol != "https" ]]; then
    echo "Error: prometheus_protocol must be either http or https"
    exit 1
  fi
  if [[ -n "${PROMETHEUS_USERNAME:-}" ]] && [[ -z "${PROMETHEUS_PASSWORD:-}" ]]; then
    echo "Error: PROMETHEUS_USERNAME is set but PROMETHEUS_PASSWORD is not. Either both must be set or unset."
    exit 1
  fi
  if [[ -z "${PROMETHEUS_USERNAME:-}" ]] && [[ -n "${PROMETHEUS_PASSWORD:-}" ]]; then
    echo "Error: PROMETHEUS_PASSWORD is set but PROMETHEUS_USERNAME is not. Either both must be set or unset."
    exit 1
  fi
}

print_backup_usage() {
  echo "Create: ${SCRIPT_NAME} create [options]"
  echo "options:"
  echo "  -o, --output                   the directory that the platform backup is written to (default: ${HOME})"
  echo "  -m, --exclude_prometheus       exclude prometheus metric data from backup (default: false)"
  echo "  -r, --exclude_releases         exclude Yugabyte releases from backup (default: false)"
  echo "  -d, --data_dir=DIRECTORY       data directory (default: /opt/yugabyte)"
  echo "  -v, --verbose                  verbose output of script (default: false)"
  echo "  -s  --skip_restart             [WARNING: DEPRECATED] don't restart processes during execution (default: false)"
  echo "  --restart                      restart processes during execution (default: false)"
  echo "  -u, --db_username=USERNAME     postgres username (default: postgres)"
  echo "  -h, --db_host=HOST             postgres host (default: localhost)"
  echo "  -P, --db_port=PORT             postgres port (default: 5432)"
  echo "  -n, --prometheus_host=HOST     prometheus host (default: localhost)"
  echo "  -t, --prometheus_port=PORT     prometheus port (default: 9090)"
  echo "  --prometheus_protocol          prometheus protocol (default: http)."
  echo "  --k8s_namespace                kubernetes namespace"
  echo "  --k8s_pod                      kubernetes pod"
  echo "  --k8s_timeout                  kubernetes cp timeout duration (default: 30m)"
  echo "  --yba_installer                yba_installer installation (default: false)"
  echo "  --plain_sql                    output a plain-text SQL script from pg_dump"
  echo "  --ybdb                         ybdb backup (default: false)"
  echo "  --ysql_dump_path               path to ysql_sump to dump ybdb"
  echo "  --disable_version_check        disable the backup version check (default: false)"
  echo "  -?, --help                     show create help, then exit"
  echo
  echo "NOTE: If prometheus authentication is enabled, PROMETHEUS_USERNAME and PROMETHEUS_PASSWORD environment variables must be set"
  echo
}

print_restore_usage() {
  echo "Restore: ${SCRIPT_NAME} restore --input <input_path> [options]"
  echo "<input_path> the path to the platform backup tar.gz"
  echo "options:"
  echo "  -o, --destination=DIRECTORY    where to un-tar the backup (default: /opt/yugabyte)"
  echo "  -d, --data_dir=DIRECTORY       data directory (default: /opt/yugabyte)"
  echo "  -v, --verbose                  verbose output of script (default: false)"
  echo "  -s  --skip_restart             don't restart processes during execution (default: false)"
  echo "  -u, --db_username=USERNAME     postgres username (default: postgres)"
  echo "  -h, --db_host=HOST             postgres host (default: localhost)"
  echo "  -P, --db_port=PORT             postgres port (default: 5432)"
  echo "  -n, --prometheus_host=HOST     prometheus host (default: localhost)"
  echo "  -t, --prometheus_port=PORT     prometheus port (default: 9090)"
  echo "  -e, --prometheus_user=USERNAME prometheus user (default: prometheus)"
  echo "  --prometheus_protocol          prometheus protocol (default: http)."
  echo "  -U, --yba_user=USERNAME        yugabyte anywhere user (default: yugabyte)"
  echo "  --k8s_namespace                kubernetes namespace"
  echo "  --k8s_pod                      kubernetes pod"
  echo "  --k8s_timeout                  kubernetes cp timeout duration (default: 30m)"
  echo "  --disable_version_check        disable the backup version check (default: false)"
  echo "  --yba_installer                yba_installer backup (default: false)"
  echo "  --ybdb                         ybdb restore (default: false)"
  echo "  --ysqlsh_path                  path to ysqlsh to restore ybdb (default: false)"
  echo "  --migration                    migration from Replicated or Yugabundle (default: false)"
  echo "  --ybai_data_dir                YBA data dir (default: /opt/yugabyte/data/yb-platform)"
  echo "  --skip_old_files               skip old files when untarring backup"
  echo "  --skip_dump_check              skip pg dump empty check before restore (default: false)"
  echo "  -?, --help                     show restore help, then exit"
  echo
  echo "NOTE: If prometheus authentication is enabled, PROMETHEUS_USERNAME and PROMETHEUS_PASSWORD environment variables must be set"
  echo
}

print_help() {
  echo "Create or restore a Yugabyte Platform backup"
  echo
  echo "Usage: ${SCRIPT_NAME} <command>"
  echo "command:"
  echo "  create                         create a Yugabyte Platform backup"
  echo "  restore                        restore a Yugabyte Platform backup"
  echo "  -?, --help                     show this help, then exit"
  echo
  print_backup_usage
  print_restore_usage
}

cleanup () {
  rm -f "$1"
}

if [[ $# -eq 0 ]]; then
  print_help
  exit 1
fi

command=$1
shift

# Default global options.
db_username=postgres
db_host=localhost
db_port=5432
prometheus_host=localhost
prometheus_port=9090
prometheus_protocol=http
prometheus_user=prometheus
k8s_namespace=""
k8s_pod=""
k8s_timeout="30m"
data_dir=/opt/yugabyte
verbose=false
disable_version_check=false
yba_installer=false
pgdump_path=""
pgpass_path=""
pgrestore_path=""
plain_sql=false
ybdb=false
ysql_dump_path=""
ysqlsh_path=""
migration=false
ybai_data_dir=/opt/yugabyte/data/yb-platform
yba_user=yugabyte
skip_old_files=""
skip_dump_check=false

case $command in
  -?|--help)
    print_help
    exit 0
    ;;
  create)
    # Default create options.
    exclude_prometheus=false
    exclude_releases=false
    output_path="${HOME}"
    RESTART_PROCESSES=false

    if [[ $# -eq 0 ]]; then
      print_backup_usage
      exit 1
    fi

    while (( "$#" )); do
      case "$1" in
        -o|--output)
          output_path=$(realpath $2)
          shift 2
          ;;
        -m|--exclude_prometheus)
          exclude_prometheus=true
          shift
          ;;
        -r|--exclude_releases)
          exclude_releases=true
          shift
          ;;
        -d|--data_dir)
          data_dir=$(realpath $2)
          shift 2
          ;;
        -v|--verbose)
          verbose=true
          set -x
          shift
          ;;
        -s|--skip_restart)
          echo "--skip_restart flag is deprecated and default behavior skips restart. use --restart"
          shift
          ;;
        --restart)
          RESTART_PROCESSES=true
          shift
          ;;
        -u|--db_username)
          db_username=$2
          shift 2
          ;;
        -h|--db_host)
          db_host=$2
          shift 2
          ;;
        -P|--db_port)
          db_port=$2
          shift 2
          ;;
        --plain_sql)
          plain_sql=true
          shift
          ;;
        -n|--prometheus_host)
          prometheus_host=$2
          shift 2
          ;;
        -t|--prometheus_port)
          prometheus_port=$2
          shift 2
          ;;
        --prometheus_protocol)
          prometheus_protocol=$2
          shift 2
          ;;
        --k8s_namespace)
          k8s_namespace=$2
          shift 2
          ;;
        --k8s_pod)
          k8s_pod=$2
          shift 2
          ;;
        --k8s_timeout)
          k8s_timeout=$2
          shift 2
          ;;
        --yba_installer)
          yba_installer=true
          shift
          ;;
        --pg_dump_path)
          pgdump_path=$(realpath $2)
          shift 2
          ;;
        --pgpass_path)
          pgpass_path=$(realpath $2)
          shift 2
          ;;
        --ybdb)
          ybdb=true
          shift
          ;;
        --ysql_dump_path)
          ysql_dump_path=$(realpath $2)
          shift 2
          ;;
        --disable_version_check)
          disable_version_check=true
          set -x
          shift
          ;;
        -?|--help)
          print_backup_usage
          exit 0
          ;;
        *)
          echo "${SCRIPT_NAME}: Unrecognized argument ${1}"
          echo
          print_backup_usage
          exit 1
      esac
    done

    validate_k8s_args "${k8s_namespace}" "${k8s_pod}"
    validate_prometheus_args

    if [[ "${pgpass_path}" != "" ]]; then
      export PGPASSFILE=${pgpass_path}
    fi
    create_backup "$output_path" "$data_dir" "$exclude_prometheus" "$exclude_releases" \
    "$db_username" "$db_host" "$db_port" "$verbose" "$prometheus_host" "$prometheus_port" \
    "$k8s_namespace" "$k8s_pod" "$pgdump_path" "$plain_sql" "$ybdb" "$ysql_dump_path" \
    "$prometheus_protocol"
    exit 0
    ;;
  restore)
    # Default restore options.
    destination=/opt/yugabyte
    input_path=""

    if [[ $# -eq 0 ]]; then
      print_restore_usage
      exit 1
    fi

    while (( "$#" )); do
      case "$1" in
        -i|--input)
          input_path=$(realpath $2)
          shift 2
          ;;
        -o|--destination)
          destination=$(realpath $2)
          shift 2
          ;;
        -d|--data_dir)
          data_dir=$(realpath $2)
          shift 2
          ;;
        -v|--verbose)
          verbose=true
          set -x
          shift
          ;;
        -s|--skip_restart)
          RESTART_PROCESSES=false
          set -x
          shift
          ;;
        -u|--db_username)
          db_username=$2
          shift 2
          ;;
        -h|--db_host)
          db_host=$2
          shift 2
          ;;
        -P|--db_port)
          db_port=$2
          shift 2
          ;;
        -n|--prometheus_host)
          prometheus_host=$2
          shift 2
          ;;
        -t|--prometheus_port)
          prometheus_port=$2
          shift 2
          ;;
        -e|--prometheus_user)
          prometheus_user=$2
          shift 2
          ;;
        --prometheus_protocol)
          prometheus_protocol=$2
          shift 2
          ;;
        --k8s_namespace)
          k8s_namespace=$2
          shift 2
          ;;
        --k8s_pod)
          k8s_pod=$2
          shift 2
          ;;
        --k8s_timeout)
          k8s_timeout=$2
          shift 2
          ;;
        --disable_version_check)
          disable_version_check=true
          set -x
          shift
          ;;
        --yba_installer)
          yba_installer=true
          DOCKER_BASED=false
          shift
          ;;
        --pg_restore_path)
          pgrestore_path=$(realpath $2)
          shift 2
          ;;
        --pgpass_path)
          pgpass_path=$(realpath $2)
          shift 2
          ;;
        --ybdb)
          ybdb=true
          shift
          ;;
        --ysqlsh_path)
          ysqlsh_path=$(realpath $2)
          shift 2
          ;;
        --yugabundle)
          echo "--yugabundle is deprecated. Please use --migration instead."
          migration=true
          shift
          ;;
        --migration)
          migration=true
          shift
          ;;
        --ybai_data_dir)
          ybai_data_dir=$(realpath $2)
          shift 2
          ;;
        -U|--yba_user)
          yba_user=$2
          shift 2
          ;;
        --use_system_pg)
          USE_SYSTEM_PG=true
          shift
          ;;
        --skip_old_files)
          skip_old_files="--skip-old-files"
          shift
          ;;
        --skip_dump_check)
          skip_dump_check=true
          shift
          ;;
        -?|--help)
          print_restore_usage
          exit 0
          ;;
        *)
          echo "${SCRIPT_NAME}: Unrecognized option ${1}"
          echo
          print_restore_usage
          exit 1
      esac
    done

    if [[ -z "$input_path" ]]; then
      echo "${SCRIPT_NAME}: input_path is required"
      echo
      print_restore_usage
      exit 1
    fi

    validate_k8s_args "${k8s_namespace}" "${k8s_pod}"
    validate_prometheus_args

    if [[ "${pgpass_path}" != "" ]]; then
      export PGPASSFILE=${pgpass_path}
    fi

    restore_backup "$input_path" "$destination" "$db_host" "$db_port" "$db_username" "$verbose" \
    "$prometheus_host" "$prometheus_port" "$data_dir" "$k8s_namespace" "$k8s_pod" \
    "$disable_version_check" "$pgrestore_path" "$ybdb" "$ysqlsh_path" "$ybai_data_dir" \
    "$skip_old_files" "$skip_dump_check" "$prometheus_protocol"
    exit 0
    ;;
  *)
    echo "${SCRIPT_NAME}: Unrecognized command ${command}"
    echo
    print_help
    exit 1
esac
