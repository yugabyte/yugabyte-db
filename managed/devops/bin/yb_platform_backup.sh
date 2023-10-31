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
if [[ "$DOCKER_BASED" = false ]] && [[ "$INSIDE_CONTAINER" = false ]]; then
  SERVICE_BASED=true
else
  SERVICE_BASED=false
fi

# Takes docker container and command as arguments. Executes docker cmd if docker-based or not.
docker_aware_cmd() {
  if [[ "$DOCKER_BASED" = false ]]; then
    $2
  else
    docker exec -i "${1}" $2
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
  if [[ "$DOCKER_BASED" = true ]]; then
    PROMETHEUS_DATA_DIR="${data_dir}/prometheusv2"
  else
    PROMETHEUS_DATA_DIR=$(curl "http://${prometheus_host}:${prometheus_port}/api/v1/status/flags" |
    ${PYTHON_EXECUTABLE} -c "import sys, json; print(json.load(sys.stdin)['data']['storage.tsdb.path'])")
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

  # Determine pg_dump path in yba-installer cases where postgres is installed in data_dir.
  if [[ "${yba_installer}" = true ]] && \
     [[ "${pgdump_path}" != "" ]] && \
     [[ -f "${pgdump_path}" ]]; then
    pg_dump="${pgdump_path}"
  fi

  if [[ "${verbose}" = true ]]; then
    backup_cmd="${pg_dump} -h ${db_host} -p ${db_port} -U ${db_username} -Fc -v --clean \
      ${PLATFORM_DB_NAME}"
  else
    backup_cmd="${pg_dump} -h ${db_host} -p ${db_port} -U ${db_username} -Fc --clean \
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
  pg_restore="pg_restore"

  # Determine pg_restore path in yba-installer cases where postgres is installed in data_dir.
  if [[ "${yba_installer}" = true ]] && \
     [[ "${pgrestore_path}" != "" ]] && \
     [[ "${USE_SYSTEM_PG}" != true ]] && \
     [[ -f "${pgrestore_path}" ]]; then
    pg_restore=${pgrestore_path}
  fi

  if [[ "${verbose}" = true ]]; then
    restore_cmd="${pg_restore} -h ${db_host} -p ${db_port} -U ${db_username} -c -v -d \
      ${PLATFORM_DB_NAME}"
  else
    restore_cmd="${pg_restore} -h ${db_host} -p ${db_port} -U ${db_username} -c -d \
      ${PLATFORM_DB_NAME}"
  fi

  # Run pg_restore.
  echo "Restoring Yugabyte Platform DB backup ${backup_path}..."
  if [[ "$migration" = true ]]; then
    set +e
  fi
  docker_aware_cmd "postgres" "${restore_cmd}" < "${backup_path}"
  set -e
  echo "Done"
}

# Deletes a Yugabyte Platform DB backup.
delete_postgres_backup() {
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
  include_releases_flag="**/releases/**"

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
      "${backup_script} create ${verbose_flag} ${exclude_flags} --output /opt/yugabyte/yugaware"
    # Determine backup archive filename.
    # Note: There is a slight race condition here. It will always use the most recent backup file.
    backup_file=$(kubectl -n "${k8s_namespace}" -c yugaware exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "cd /opt/yugabyte/yugaware && ls -1 backup*.tgz | tail -n 1")
    backup_file=${backup_file%$'\r'}
    # Ensure backup succeeded.
    if [[ -z "${backup_file}" ]]; then
      echo "Failed"
      return
    fi

    # The version_metadata.json file is always present in the container, so
    # we don't need to check if the file exists before copying it to the output path.

    # Note that the copied path is version_metadata_backup.json and not version_metadata.json
    # because executing yb_platform_backup.sh with the kubectl command will first execute the
    # script in the container, making the version metadata file already be renamed to
    # version_metadata_backup.json and placed at location
    # /opt/yugabyte/yugaware/version_metadata_backup.json in the container. We extract this file
    # from the container to our local machine for version checking.

    version_path="/opt/yugabyte/yugaware/${VERSION_METADATA_BACKUP}"


    # Copy version_metadata_backup.json from container to local machine.
    kubectl cp "${k8s_pod}:${version_path}" "${output_path}/${VERSION_METADATA_BACKUP}" \
      -n "${k8s_namespace}" -c yugaware

    # Delete version_metadata_backup.json from container.
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "rm /opt/yugabyte/yugaware/${VERSION_METADATA_BACKUP}"

    echo "Copying backup from container"
    # Copy backup archive from container to local machine.
    kubectl -n "${k8s_namespace}" -c yugaware cp \
      "${k8s_pod}:${backup_file}" "${output_path}/${backup_file}"

    # Delete backup archive from container.
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "rm /opt/yugabyte/yugaware/backup*.tgz"
    echo "Done"
    return
  fi



  if [ "$disable_version_check" != true ]; then

    metadata_regex="**/yugaware/conf/${VERSION_METADATA}"
    if [[ "${yba_installer}" = true ]]; then
      version=$(basename $(realpath /opt/yugabyte/software/active))
      metadata_regex="**/${version}/**/yugaware/conf/${VERSION_METADATA}"
    fi
    version_path=$(docker_aware_cmd "yugaware" "find ${data_dir} -wholename ${metadata_regex}")

    # At least keep some default as a worst case.
    if [ ! -f ${version_path} ] || [ -z ${version_path} ]; then
      version_path="/opt/yugabyte/yugaware/conf/${VERSION_METADATA}"
    fi

    command="cp ${version_path} ${data_dir}/${VERSION_METADATA_BACKUP}"
    docker_aware_cmd "yugaware" "${command}"
  fi

  if [[ "$exclude_releases" = true ]]; then
    include_releases_flag=""
  fi

  modify_service yb-platform stop

  tar_name="${output_path}/backup_${now}.tar"
  tgz_name="${output_path}/backup_${now}.tgz"
  db_backup_path="${data_dir}/${PLATFORM_DUMP_FNAME}"
  trap 'delete_postgres_backup ${db_backup_path}' RETURN
  create_postgres_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
                         "${verbose}" "${yba_installer}" "${pgdump_path}"

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
              "${include_releases_flag}") )

  # Backup prometheus data.
  if [[ "$exclude_prometheus" = false ]]; then
    trap 'run_sudo_cmd "rm -rf ${data_dir}/${PROMETHEUS_SNAPSHOT_DIR}"' RETURN
    echo "Creating prometheus snapshot..."
    set_prometheus_data_dir "${prometheus_host}" "${prometheus_port}" "${data_dir}"
    snapshot_dir=$(curl -X POST "http://${prometheus_host}:${prometheus_port}/api/v1/admin/tsdb/snapshot" |
      ${PYTHON_EXECUTABLE} -c "import sys, json; print(json.load(sys.stdin)['data']['name'])")
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

  eval find ${FIND_OPTIONS[@]}

  gzip -9 < ${tar_name} > ${tgz_name}
  cleanup "${tar_name}"

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
  ybai_data_dir="${14}"
  prometheus_dir_regex="\.\/${PROMETHEUS_SNAPSHOT_DIR}\/[[:digit:]]{8}T[[:digit:]]{6}Z-[[:alnum:]]{16}\/$"

  current_metadata_path=""

  if [ -f "../../src/main/resources/${VERSION_METADATA}" ]; then

      current_metadata_path="../../src/main/resources/${VERSION_METADATA}"

  else

      metadata_regex="**/yugaware/conf/${VERSION_METADATA}"
      if [[ "${yba_installer}" = true ]]; then
        version=$(basename $(realpath ${data_dir}/software/active))
        metadata_regex="**/${version}/**/yugaware/conf/${VERSION_METADATA}"
      fi
      current_metadata_path=$(find ${destination} -wholename ${metadata_regex})

      # At least keep some default as a worst case.
      if [ ! -f ${current_metadata_path} ] || [ -z ${current_metadata_path} ]; then
        current_metadata_path="/opt/yugabyte/yugaware/conf/${VERSION_METADATA}"
      fi

  fi


  # Perform K8s restore.
  if [[ -n "${k8s_namespace}" ]] || [[ -n "${k8s_pod}" ]]; then

    # Copy backup archive to container.
    echo "Copying backup to container"
    kubectl -n "${k8s_namespace}" -c yugaware cp \
      "${input_path}" "${k8s_pod}:/opt/yugabyte/yugaware/"
    echo "Done"

    # Copy version_metadata_backup.json to container.
    kubectl -n "${k8s_namespace}" -c yugaware cp \
      "${r_pth}" "${k8s_pod}:/opt/yugabyte/yugaware/"

    # Determine backup archive filename.
    # Note: There is a slight race condition here. It will always use the most recent backup file.
    backup_file=$(kubectl -n "${k8s_namespace}" -c yugaware exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "cd /opt/yugabyte/yugaware && ls -1 backup*.tgz | tail -n 1")
    backup_file=${backup_file%$'\r'}
    # Run restore script in container.
    verbose_flag=""
    if [[ "${verbose}" == true ]]; then
      verbose_flag="-v"
    fi
    backup_script="/opt/yugabyte/devops/bin/yb_platform_backup.sh"

    #Passing in the required argument for --disable_version_check if set to true, since
    #the script is called again within the Kubernetes container.
    d="--disable_version_check"
    cont_path="/opt/yugabyte/yugaware"

    if [ "$disable_version_check" != true ]; then
      kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- /bin/bash -c \
        "${backup_script} restore ${verbose_flag} --input ${cont_path}/${backup_file}"
    else
      kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- /bin/bash -c \
        "${backup_script} restore ${verbose_flag} --input ${cont_path}/${backup_file} ${d}"
    fi

    # Delete version_metadata_backup.json from container.
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "rm /opt/yugabyte/yugaware/${VERSION_METADATA_BACKUP}"

    # Delete version_metadata.json from container (it already exists at conf folder)
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "rm /opt/yugabyte/yugaware/${VERSION_METADATA}"

    # Delete backup archive from container.
    kubectl -n "${k8s_namespace}" exec -it "${k8s_pod}" -c yugaware -- \
      /bin/bash -c "rm /opt/yugabyte/yugaware/backup*.tgz"
    return
  fi

  if [ "$disable_version_check" != true ]; then
    command="cat ${current_metadata_path}"

    version_cmd="import json, sys; print(json.load(sys.stdin)[\"version_number\"])"

    build_cmd="import json, sys; print(json.load(sys.stdin)[\"build_number\"])"

    version=$(docker_aware_cmd "yugaware" "${command}" | ${PYTHON_EXECUTABLE} -c "${version_cmd}")
    build=$(docker_aware_cmd "yugaware" "${command}" | ${PYTHON_EXECUTABLE} -c "${build_cmd}")

    curr_platform_version=${version}-${build}

    backup_metadata_path=$(tar -tzf ${input_path} | grep ${VERSION_METADATA_BACKUP} | head -1)
    tar -xzf ${input_path} ${backup_metadata_path}
    # The version_metadata.json file is always present in a release package, and it would have
    # been stored during create_backup(), so we don't need to check if the file exists before
    # restoring it from the restore path.
    backup_yba_version="cat ${backup_metadata_path} | ${PYTHON_EXECUTABLE} -c ${version_cmd}"
    backup_yba_build="cat ${backup_metadata_path} | ${PYTHON_EXECUTABLE} -c ${build_cmd}"
    back_plat_version=${backup_yba_version}-${backup_yba_build}

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

  db_backup_path="${destination}/${PLATFORM_DUMP_FNAME}"
  yugabackup="${destination}"/"${MIGRATION_BACKUP_DIR}"
  trap 'delete_db_backup ${db_backup_path}' RETURN
  tar_cmd="tar -xzf"
  if [[ "${verbose}" = true ]]; then
    tar_cmd="tar -xzvf"
  fi
  if [[ "${migration}" = true ]]; then
    # Copy over migration backup data into the correct yba-installer paths
    db_backup_path="${yugabackup}"/"${PLATFORM_DUMP_FNAME}"
    rm -rf "${yugabackup}"
    mkdir -p "${yugabackup}"
    $tar_cmd "${input_path}" --directory "${yugabackup}"

    # Copy over releases. Need to ignore node-agent/ybc releases
    set +e
    releasesdir=$(find "${yugabackup}" -name "releases" -type d | \
                  grep -v "ybc" | grep -v "node-agent")
    set -e
    if [[ "$releasesdir" != "" ]] && [[ -d "$releasesdir" ]]; then
      cp -R "$releasesdir" "$ybai_data_dir"
    fi
    # Node-agent/ybc foldes can be copied entirely into
    # Copy releases, ybc, certs, keys, over
    # xcerts/keys/licenses can all go directly into data directory
    BACKUP_DIRS=('*ybc' '*data/certs' '*data/keys' '*data/licenses' '*node-agent')
    for d in "${BACKUP_DIRS[@]}"
    do
      set +e
      found_dir=$(find "${yugabackup}" -path "$d" -type d)
      set -e
      if [[ "$found_dir" != "" ]] && [[ -d "$found_dir" ]]; then
        cp -R "$found_dir" "$ybai_data_dir"
      fi
    done
  else
    $tar_cmd "${input_path}" --directory "${destination}"
  fi

  restore_postgres_backup "${db_backup_path}" "${db_username}" "${db_host}" "${db_port}" \
  "${verbose}" "${yba_installer}" "${pgrestore_path}"
  # Restore prometheus data.
  set +e
  prom_snapshot=$(tar -tf "${input_path}" | grep -E $prometheus_dir_regex)
  set -e
  if [[ -n "$prom_snapshot" ]]; then
    echo "Restoring prometheus snapshot..."
    set_prometheus_data_dir "${prometheus_host}" "${prometheus_port}" "${data_dir}"
    modify_service prometheus stop
    # Find snapshot directory in backup
    run_sudo_cmd "rm -rf ${PROMETHEUS_DATA_DIR}/*"
    # snapshot_dir="${destination}/'${prom_snapshot:2}'*"
    run_sudo_cmd "mv ${destination}/'${prom_snapshot:2}'* ${PROMETHEUS_DATA_DIR}"
    if [[ "${yba_installer}" = true ]] && [[ "${migration}" = true ]]; then
      backup_targets=$(find "${yugabackup}" -name swamper_targets -type d)
      if  [[ "$backup_targets" != "" ]] && [[ -d "$backup_targets" ]]; then
        run_sudo_cmd "cp -Tr ${backup_targets} ${destination}/data/prometheus/swamper_targets"
      fi
      backup_rules=$(find "${yugabackup}" -name swamper_rules -type d)
      if  [[ "$backup_rules" != "" ]] && [[ -d "$backup_rules" ]]; then
        run_sudo_cmd "cp -Tr ${backup_rules} ${destination}/data/prometheus/swamper_rules"
      fi
      run_sudo_cmd "chown -R ${yba_user}:${yba_user} ${destination}/data/prometheus"
    elif [[ "${yba_installer}" = true ]]; then
      run_sudo_cmd "chown -R ${yba_user}:${yba_user} ${destination}/data/prometheus"
    fi
    if [[ "$SERVICE_BASED" = true ]]; then
      run_sudo_cmd "chown -R ${prometheus_user}:${prometheus_user} ${PROMETHEUS_DATA_DIR}"
    else
      run_sudo_cmd "chown -R ${NOBODY_UID}:${NOBODY_UID} ${PROMETHEUS_DATA_DIR}"
    fi
    # Clean up snapshot after restore
    run_sudo_cmd "rm -rf ${destination}/${PROMETHEUS_SNAPSHOT_DIR}"
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
  if [[ "$yba_installer" = true ]]; then
    run_sudo_cmd "chown -R ${yba_user}:${yba_user} ${ybai_data_dir}"
  fi

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

print_backup_usage() {
  echo "Create: ${SCRIPT_NAME} create [options]"
  echo "options:"
  echo "  -o, --output                   the directory that the platform backup is written to (default: ${HOME})"
  echo "  -m, --exclude_prometheus       exclude prometheus metric data from backup (default: false)"
  echo "  -r, --exclude_releases         exclude Yugabyte releases from backup (default: false)"
  echo "  -d, --data_dir=DIRECTORY       data directory (default: /opt/yugabyte)"
  echo "  -v, --verbose                  verbose output of script (default: false)"
  echo "  -s  --skip_restart             don't restart processes during execution (default: false)"
  echo "  -u, --db_username=USERNAME     postgres username (default: postgres)"
  echo "  -h, --db_host=HOST             postgres host (default: localhost)"
  echo "  -P, --db_port=PORT             postgres port (default: 5432)"
  echo "  -n, --prometheus_host=HOST     prometheus host (default: localhost)"
  echo "  -t, --prometheus_port=PORT     prometheus port (default: 9090)"
  echo "  --k8s_namespace                kubernetes namespace"
  echo "  --k8s_pod                      kubernetes pod"
  echo "  --yba_installer                yba_installer installation (default: false)"
  echo "  -?, --help                     show create help, then exit"
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
  echo "  -U, --yba_user=USERNAME        yugabyte anywhere user (default: yugabyte)"
  echo "  --k8s_namespace                kubernetes namespace"
  echo "  --k8s_pod                      kubernetes pod"
  echo "  --disable_version_check        disable the backup version check (default: false)"
  echo "  --yba_installer                yba_installer backup (default: false)"
  echo "  --migration                    migration from Replicated or Yugabundle (default: false)"
  echo "  --ybai_data_dir                YBA data dir (default: /opt/yugabyte/data/yb-platform)"
  echo "  -?, --help                     show restore help, then exit"
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
prometheus_user=prometheus
k8s_namespace=""
k8s_pod=""
data_dir=/opt/yugabyte
verbose=false
disable_version_check=false
yba_installer=false
pgdump_path=""
pgpass_path=""
pgrestore_path=""
migration=false
ybai_data_dir=/opt/yugabyte/data/yb-platform
yba_user=yugabyte

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

    if [[ $# -eq 0 ]]; then
      print_backup_usage
      exit 1
    fi

    while (( "$#" )); do
      case "$1" in
        -o|--output)
          output_path=$2
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
          data_dir=$2
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
        --k8s_namespace)
          k8s_namespace=$2
          shift 2
          ;;
        --k8s_pod)
          k8s_pod=$2
          shift 2
          ;;
        --yba_installer)
          yba_installer=true
          shift
          ;;
        --pg_dump_path)
          pgdump_path=$2
          shift 2
          ;;
        --pgpass_path)
          pgpass_path=$2
          shift 2
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

    if [[ "${pgpass_path}" != "" ]]; then
      export PGPASSFILE=${pgpass_path}
    fi
    create_backup "$output_path" "$data_dir" "$exclude_prometheus" "$exclude_releases" \
    "$db_username" "$db_host" "$db_port" "$verbose" "$prometheus_host" "$prometheus_port" \
    "$k8s_namespace" "$k8s_pod" "$pgdump_path"
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
          input_path=$2
          shift 2
          ;;
        -o|--destination)
          destination=$2
          shift 2
          ;;
        -d|--data_dir)
          data_dir=$2
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
        --k8s_namespace)
          k8s_namespace=$2
          shift 2
          ;;
        --k8s_pod)
          k8s_pod=$2
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
          pgrestore_path=$2
          shift 2
          ;;
        --pgpass_path)
          pgpass_path=$2
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
          ybai_data_dir=$2
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

    if [[ "${pgpass_path}" != "" ]]; then
      export PGPASSFILE=${pgpass_path}
    fi

    restore_backup "$input_path" "$destination" "$db_host" "$db_port" "$db_username" "$verbose" \
    "$prometheus_host" "$prometheus_port" "$data_dir" "$k8s_namespace" "$k8s_pod" \
    "$disable_version_check" "$pgrestore_path" "$ybai_data_dir"
    exit 0
    ;;
  *)
    echo "${SCRIPT_NAME}: Unrecognized command ${command}"
    echo
    print_help
    exit 1
esac
