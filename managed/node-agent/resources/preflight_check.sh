#!/bin/bash
#
# Copyright 2020 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

check_type="provision"
node_agent_mode=false
airgap=false
install_node_exporter=true
skip_ntp_check=false
mount_points=""
yb_home_dir="/home/yugabyte"
# This should be a comma separated key-value list. Associative arrays were add in bash 4.0 so
# they might not exist in the provided instance depending on how old it is.
result_kvs=""
ssh_port=""
package_manager_cmd=""
is_aarch64=false
is_debian=false
master_http_port="7000"
master_rpc_port="7100"
tserver_http_port="9000"
tserver_rpc_port="9100"
yb_controller_http_port="14000"
yb_controller_rpc_port="18018"
redis_server_http_port="11000"
redis_server_rpc_port="6379"
ycql_server_http_port="12000"
ycql_server_rpc_port="9042"
ysql_server_http_port="13000"
ysql_server_rpc_port="5433"
node_exporter_port="9300"

preflight_provision_check() {
  # Check for internet access.
  if [[ "$airgap" = false ]]; then
    # Attempt to run "/dev/tcp" 3 times with a 3 second timeout and return success if any succeed.
    # --preserve-status flag will maintain the exit code of the "/dev/tcp" command.
    HOST="yugabyte.com"
    PORT=443

    for i in 1 2 3; do
      timeout 3 bash -c "cat < /dev/null > /dev/tcp/${HOST}/${PORT}" --preserve-status && break
    done
    update_result_json_with_rc "internet_connection" "$?"
  fi

  # Check for yugabyte in AllowUsers in /etc/ssh/sshd_confign
  if grep -q "AllowUsers" /etc/ssh/sshd_config; then
    egrep -q 'AllowUsers.* yugabyte( |@|$)' /etc/ssh/sshd_config
    update_result_json_with_rc "allow_users_yugabyte" "$?"
  fi

  if [[ $install_node_exporter = true ]]; then
    # Check node exporter isn't already installed.
    no_node_exporter=false
    if [[ "$(ps -ef | grep "node_exporter" | grep -v "grep" | grep -v "preflight" |
             wc -l | tr -d ' ')" = '0' ]]; then
      no_node_exporter=true
    fi
    update_result_json "prometheus_no_node_exporter" "$no_node_exporter"

    # Check prometheus files are writable.

    for path in $filepaths; do
      check_filepath "prometheus" "$path" true
    done

    check_free_space "prometheus_space" "/opt/prometheus"
    check_free_space "tmp_dir_space" "/tmp"
    check_port "node_exporter_port" "$node_exporter_port"
  fi

  # Check ulimit settings.
  ulimit_filepath="/etc/security/limits.conf"
  check_filepath "pam_limits_writable" $ulimit_filepath true

  # Check if chrony is running
  if [[ $(systemctl status chronyd | grep -c "active (running)") = 1 ]]; then
    update_result_json "chronyd_running" true
  else
    update_result_json "chronyd_running" false
  fi

  # Check ssh port is available.
  check_port "ssh_port" "$ssh_port"

  # Get the number of cores.
  result=$(nproc --all 2>&1)
  update_result_json "cpu_cores" "$result"

  # Ram size (In MB).
  result=$(free -m | grep Mem: | awk '{print $2}' 2>&1)
  update_result_json "ram_size" "$result"

  # Check sudo access.
  result=$(sudo -n ls)
  if [[ "$?" -eq 0 ]]; then
    update_result_json "sudo_access" true
  else
    update_result_json "sudo_access" false
  fi

  # Check Locale
  result=$(locale -a | grep -q -E "en_US.utf8|en_US.UTF-8")
  if [[ "$?" -eq 0 ]]; then
    update_result_json "locale_present" true
  else
    update_result_json "locale_present" false
  fi
}

check_ntp_synchronization() {
  if [[ "$skip_ntp_check" = false ]]; then
    ntp_status=$(timedatectl status)
    ntp_check=true
    enabled_regex='(NTP enabled: |NTP service: |Network time on: )([^'$'\n'']*)'
    if [[ $ntp_status =~ $enabled_regex ]]; then
      enabled_status="${BASH_REMATCH[2]// /}"
      if [[ "$enabled_status" != "yes" ]] && [[ "$enabled_status" != "active" ]]; then
        # Oracle8 has the line NTP service: n/a instead. Don't fail if this line exists
        if [[ ! ("${BASH_REMATCH[1]}" == "NTP service: " \
              && "${BASH_REMATCH[2]// /}" == "n/a") ]]; then
          ntp_check=false
        fi
      fi
    else
      systemd_regex='systemd-timesyncd.service active:'
      if [[ ! $ntp_status =~ $systemd_regex ]]; then # See PLAT-3373
        ntp_check=false
      fi
    fi
    synchro_regex='(NTP synchronized: |System clock synchronized: )([^'$'\n'']*)'
    if [[ $ntp_status =~ $synchro_regex ]]; then
      synchro_status="${BASH_REMATCH[2]// /}"
      if [[ "$synchro_status" != "yes" ]]; then
        ntp_check=false
      fi
    else
      ntp_check=false
    fi
    # Check if one of chronyd, ntpd and systemd-timesyncd is running on the node
    service_regex="Active: active \(running\)"
    service_check=false
    for ntp_service in chronyd ntp ntpd systemd-timesyncd; do
      service_status=$(systemctl status $ntp_service)
      if [[ $service_status =~ $service_regex ]]; then
        service_check=true
        break
      fi
    done
    if $service_check && $ntp_check; then
      update_result_json "ntp_service_status" true
    else
      update_result_json "ntp_service_status" false
    fi
  fi
}

check_yugabyte_user() {
  # Get user
  result=$(id -nu 2>&1)
  update_result_json "user" "$result"

  # Get Group
  result=$(id -gn 2>&1)
  update_result_json "user_group" "$result"
}

check_packages_installed() {

  check_package_installed "openssl" # Required.
  check_package_installed "policycoreutils" # Required.
  check_package_installed "rsync" # Optional.
  check_package_installed "xxhash" # Optional.

  if [[ "$is_aarch64" = true  && "$is_debian" = true ]]; then
    check_package_installed "libatomic1" # Required.
    check_package_installed "libncurses6" # Required.
  elif [[ "$is_aarch64" == true ]]; then
    check_package_installed "libatomic" # Required.
  fi
}

check_package_installed() {
  package_name=$1
  is_package_installed=false
  result="$($package_manager_cmd | grep $package_name)"
  if [[ -n "$result" ]]; then
    is_package_installed=true
  fi
  update_result_json "$package_name" "$is_package_installed"
}

check_binaries_installed() {
  check_binary_installed "chronyc"
  if [[ "$is_aarch64" = false ]]; then
    check_binary_installed "azcopy" # Optional.
  fi

  check_binary_installed "gsutil" # Optional.
  check_binary_installed "s3cmd" # Optional.
}

check_binary_installed() {
  binary_name=$1
  command -v $binary_name >/dev/null 2>&1;
  update_result_json_with_rc "$binary_name" "$?"
}

preflight_configure_check() {

  check_packages_installed

  check_binaries_installed

  # Check that node exporter is running.
  node_exporter_running=false
  if [[ "$(ps -ef | grep "node_exporter" | grep -v "grep" | grep -v "preflight" |
            wc -l | tr -d ' ')" == '1' ]]; then
    node_exporter_running=true
  fi
  update_result_json "node_exporter_running" "$node_exporter_running"


  # Check that node exporter is listening on correct port (default: 9300).
  running_port=$(ps -ef | grep "node_exporter" | grep -v "grep" |
            grep -v "preflight" | grep -oP '(?<=web.listen-address=:)\w+')
  check_passed=false
  if [[ "$running_port" == "$node_exporter_port" ]]; then
    check_passed=true
  fi
  update_result_json "node_exporter_port:$node_exporter_port" "$check_passed"

  # Check swappiness (optional).
  swappiness=$(cat /proc/sys/vm/swappiness)
  update_result_json "swappiness" "$swappiness"

  # Check ulimit values for core, nofile, and nproc.
  ulimit_core=$(ulimit -c)
  update_result_json "ulimit_core" "$ulimit_core"
  ulimit_open_files=$(ulimit -n)
  update_result_json "ulimit_open_files" "$ulimit_open_files"
  ulimit_user_processes=$(ulimit -u)
  update_result_json "ulimit_user_processes" "$ulimit_user_processes"

  # Check systemd sudoers.
  # TODO change the check name appropriately.
  timeout 5 sudo -n /bin/systemctl --no-ask-password enable yb-master
  if [[ "$?" = 0 ]]; then
    update_result_json "systemd_sudoer_entry" true
  else
    timeout 5 systemctl --user --no-ask-password enable yb-master
    if [[ "$?" = 0 ]]; then
      update_result_json "systemd_sudoer_entry" true
    else
      update_result_json "systemd_sudoer_entry" false
    fi
  fi

  # Check virtual memory max map limit.
  vm_max_map_count=$(cat /proc/sys/vm/max_map_count 2> /dev/null)
  update_result_json "vm_max_map_count" "${vm_max_map_count:-0}"
}

preflight_all_checks() {
  # Check python is installed.
  result=$(/bin/sh -c "/usr/bin/env python --version" 2>&1)
  if [[ $? != 0 ]]; then
    result="-1"
  fi
  update_result_json "python_version" "$result"

  # Check home directory exists.
  if [[ -d "$yb_home_dir" ]]; then
    update_result_json "home_dir_exists" true
  else
    update_result_json "home_dir_exists" false
  fi

  # Check all the communication ports
  check_port "master_http_port" "$master_http_port"
  check_port "master_rpc_port" "$master_rpc_port"
  check_port "tserver_http_port" "$tserver_http_port"
  check_port "tserver_rpc_port" "$tserver_rpc_port"
  check_port "yb_controller_http_port" "$yb_controller_http_port"
  check_port "yb_controller_rpc_port" "$yb_controller_rpc_port"
  check_port "redis_server_http_port" "$redis_server_http_port"
  check_port "redis_server_rpc_port" "$redis_server_rpc_port"
  check_port "ycql_server_http_port" "$ycql_server_http_port"
  check_port "ycql_server_rpc_port" "$ycql_server_rpc_port"
  check_port "ysql_server_http_port" "$ysql_server_http_port"
  check_port "ysql_server_rpc_port" "$ysql_server_rpc_port"

  # Check mount points volume size.
  IFS="," read -ra mount_points_arr <<< "$mount_points"
  for path in "${mount_points_arr[@]}"; do
    volume=$(df -m "$path" | awk 'FNR == 2 {print $4}' 2>&1)
    update_result_json "mount_points_volume:$path" "$volume"
  done

  # Check mount points are writeable/exist.
  IFS="," read -ra mount_points_arr <<< "$mount_points"
  for path in "${mount_points_arr[@]}"; do
    check_filepath "mount_points_writable" "$path" false
  done

  check_ntp_synchronization

  check_yugabyte_user

  check_free_space "home_dir_space" "$yb_home_dir"

  # Check whether files in home directory are cleaned up if exists.
  # Finds all files including the .yugabytedb folder and ignores other dot files, node-agent files.
  yb_home_dir_clean=true
  if [[ -e "$yb_home_dir" ]]; then
    if [[ "$check_type" == "provision" ]]; then
      files=$(sudo find "$yb_home_dir" -maxdepth 1 -mindepth 1  -name "bin" -o -name ".yugabytedb" -o -name "controller" -o -name "master" -o -name "tserver")
    else
      files=$(find "$yb_home_dir" -maxdepth 1 -mindepth 1  -name "bin" -o -name ".yugabytedb" -o -name "controller" -o -name "master" -o -name "tserver")
    fi
    if [[ -n "$files" ]]; then
      yb_home_dir_clean=false
    fi
  fi
  update_result_json "yb_home_dir_clean" "$yb_home_dir_clean"

  # Check whether files in data directory are cleaned up.
  data_dir_clean=true
  processes=("tserver" "master" "controller")
  IFS="," read -ra mount_points_arr <<< "$mount_points"
  for path in "${mount_points_arr[@]}"; do
    if [ -e "$path/pg_data" ]; then
      data_dir_clean=false
      break
    fi
    if [ -e "$path/ybc-data" ]; then
      data_dir_clean=false
      break
    fi
    for daemon in "${processes[@]}"; do
      if [ -e "$path/yb-data/$daemon" ]; then
        data_dir_clean=false
        break
      fi
    done
  done
  update_result_json "data_dir_clean" "$data_dir_clean"
}

check_port() {
  name=$1
  port=$2

  check_passed=true
  if ss -lntu | grep -q ":$port\s"; then
    check_passed=false
  fi
  update_result_json "$name:$port" "$check_passed"
}

# Checks if given filepath is writable.
check_filepath() {
  test_type="$1"
  path="$2"
  check_parent="$3" # If true, will check parent directory is writable if given path doesn't exist.

  test -w "$path" || ($check_parent && test -w $(dirname "$path"))
  update_result_json_with_rc "$test_type:$path" "$?"
}

check_free_space() {
  test_type="$1"
  path="$2"
  # Check parent if path does not exist.
  if [ ! -w "$path" ]; then
    path=$(dirname "$path")
  fi

  result=$(df -m "$path" 2>&1)

  # If parent does not exist, set space to 0.
  if [ $? == "1" ]; then
    update_result_json "$test_type:$path" 0
  else
    result=$(df -m "$path" | awk 'FNR == 2 {print $4}' 2>&1)
    update_result_json "$test_type:$path" "$result"
  fi
}

update_result_json() {
  if [[ -z "$result_kvs" ]]; then
    result_kvs="\"${1}\":{\"value\":\"${2}\"}"
  else
    result_kvs="${result_kvs},\"${1}\":{\"value\":\"${2}\"}"
  fi
}

update_result_json_with_rc() {
  # Input: test_name, returncode
  check_passed=false
  if [[ "$2" == "0" ]]; then
    check_passed=true
  fi
  update_result_json "$1" "$check_passed"
}

setup() {
  if [ -n "$(command -v yum)" ]; then
    package_manager_cmd="yum list installed"
  elif [ -n "$(command -v apt-get)" ]; then
    package_manager_cmd="apt list --installed"
  else
    err_msg "Yum and Apt do not exist"
    exit 1
  fi

  # Determine whether we are using ARM64 architecture.
  arch=$(uname -m)
  if [[ "$arch" == "aarch64" ]]; then
    is_aarch64=true
  fi

  # Determine whether we are using Debian linux distribution.
  if [[ $(grep '^ID=' /etc/os-release |  cut -d= -f2 |
        sed -e 's/^"//' -e 's/"$//') = 'debian' ]]; then
    is_debian=true
  fi
}

show_usage() {
  cat <<-EOT
Usage: ${0##*/} --type {configure,provision} [<options>]

Options:
  -t, --type (REQUIRED)
    Type of preflight check to perform. Must be in ['configure', 'provision'].
  --airgap
    Skip internet access check.
  --install_node_exporter
    Check if node exporter files are accessible.
  --skip_ntp_check
    Skip check for time synchronization.
  --mount_points MOUNT_POINTS
    Commas separated list of mount paths to check permissions of.
  --yb_home_dir HOME_DIR
    Home directory of yugabyte user.
  --ssh_port SSH_PORT
    Ssh port for the node
  --cleanup
    Deletes this script after being run. Allows `scp` commands to port over new preflight scripts.
  -h, --help
    Show usage.
EOT
}

err_msg() {
  echo "$@" >&2
}

if [[ ! $# -gt 0 ]]; then
  show_usage
  exit 1
fi

while [[ $# -gt 0 ]]; do
  case $1 in
    -t|--type)
      options="provision configure"
      if [[ ! $options =~ (^|[[:space:]])"$2"($|[[:space:]]) ]]; then
        err_msg "Invalid option: $2. Must be one of ['configure', 'provision'].\n"
        show_usage >&2
        exit 1
      fi
      check_type="$2"
      shift
    ;;
    --node_agent_mode)
      node_agent_mode=true
    ;;
    --airgap)
      airgap=true
    ;;
    --install_node_exporter)
      install_node_exporter=true
    ;;
    --skip_ntp_check)
      skip_ntp_check=true
    ;;
    --mount_points)
      mount_points="$2"
      shift
    ;;
    --master_http_port)
      master_http_port="$2"
      shift
    ;;
    --master_rpc_port)
      master_rpc_port="$2"
      shift
    ;;
    --tserver_http_port)
      tserver_http_port="$2"
      shift
    ;;
    --tserver_rpc_port)
      tserver_rpc_port="$2"
      shift
    ;;
    --yb_controller_http_port)
      yb_controller_http_port="$2"
      shift
    ;;
    --yb_controller_rpc_port)
      yb_controller_rpc_port="$2"
      shift
    ;;
    --redis_server_http_port)
      redis_server_http_port="$2"
      shift
    ;;
    --redis_server_rpc_port)
      redis_server_rpc_port="$2"
      shift
    ;;
    --ycql_server_http_port)
      ycql_server_http_port="$2"
      shift
    ;;
    --ycql_server_rpc_port)
      ycql_server_rpc_port="$2"
      shift
    ;;
    --ysql_server_http_port)
      ysql_server_http_port="$2"
      shift
    ;;
    --ysql_server_rpc_port)
      ysql_server_rpc_port="$2"
      shift
    ;;
    --node_exporter_port)
      node_exporter_port="$2"
      shift
    ;;
    --ssh_port)
      ssh_port="$2"
      shift
    ;;
    --yb_home_dir)
      yb_home_dir=${2//\'/}
      shift
    ;;
    --cleanup)
      trap "rm -- $0" EXIT
    ;;
    -h|--help)
      show_usage >&2
      exit 1
    ;;
    *)
      err_msg "Invalid option: $1\n"
      show_usage >&2
      exit 1
  esac
  shift
done


setup >/dev/null 2>&1
preflight_all_checks >/dev/null 2>&1

if [[ "$check_type" == "provision" ]]; then
  preflight_provision_check >/dev/null 2>&1
else
  preflight_configure_check >/dev/null 2>&1
fi

if [[ "$node_agent_mode" == "false" ]]; then
  python - "{$result_kvs}" <<EOF
import json
import sys
dict=json.loads(sys.argv[1])
keys = list(dict.keys())
keys.sort()
idx = 1
for key in keys:
  val = dict[key].get("value")
  print('{}. {} is {}'.format(idx, key, val))
  idx += 1
EOF
else
  echo "{$result_kvs}"
fi
