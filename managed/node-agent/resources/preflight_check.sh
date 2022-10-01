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
airgap=false
install_node_exporter=true
skip_ntp_check=false
mount_points=""
yb_home_dir="/home/yugabyte"
# This should be a comma separated key-value list. Associative arrays were add in bash 4.0 so
# they might not exist in the provided instance depending on how old it is.
result_kvs=""
ports_to_check=""

preflight_provision_check() {
  # Check python is installed.
  result=$(/bin/sh -c "/usr/bin/env python --version" 2>&1)
  #update_result_json_with_rc "Access to Python" "$?"
  update_result_json_with_val_err "python_version" "$result" $?


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
    update_result_json_with_val_err "prometheus_no_node_exporter" "$no_node_exporter" "0"

    # Check prometheus files are writable.

    for path in $filepaths; do
      check_filepath "prometheus" "$path" true
    done

    check_free_space "prometheus_space" "/opt/prometheus"
    check_free_space "tmp_dir_space" "/tmp"
  fi

  # Check ulimit settings.
  ulimit_filepath="/etc/security/limits.conf"
  check_filepath "pam_limits_writable" $ulimit_filepath true

  # Check NTP synchronization.
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
      update_result_json_with_val_err "ntp_service_status" true "0"
    else
      update_result_json_with_val_err "ntp_service_status" false "0"
    fi
  fi

  # Check mount points are writeable.
  IFS="," read -ra mount_points_arr <<< "$mount_points"
  for path in "${mount_points_arr[@]}"; do
    check_filepath "mount_points" "$path" false
  done

  # Check ports are available.
  IFS="," read -ra ports_to_check_arr <<< "$ports_to_check"
  for port in "${ports_to_check_arr[@]}"; do
    check_passed=true
    if netstat -tulpn | grep ":$port\s"; then
      check_passed=false
    fi
    update_result_json_with_val_err "ports:$port" "$check_passed" "0"
  done

  check_free_space "home_dir_space" "$yb_home_dir"

  check_yugabyte_user

  #Get the number of cores
  result=$(nproc --all 2>&1)
  update_result_json_with_val_err "cpu_cores" "$result" $?

  #Ram size
  result=$(free -m 2>&1)
  if [ "$?" = "0" ]; then
    result=$(free -m | grep Mem: | awk '{print $2}' 2>&1) #in MB
    update_result_json_with_val_err "ram_size" "$result" 0
  else
    update_result_json_with_val_err "ram_size" "$result" 1
  fi
}

check_yugabyte_user() {
  # Get user
  result=$(id -nu 2>&1)
  update_result_json_with_val_err "user" "$result" "$?"

  # Get Group
  result=$(id -gn 2>&1)
  update_result_json_with_val_err "user_group" "$result" "$?"
}

preflight_configure_check() {
  check_yugabyte_user
  # Check home directory exists.
  check_filepath "home_dir_space" "$yb_home_dir" false
}

# Checks if given filepath is writable.
check_filepath() {
  test_type="$1"
  path="$2"
  check_parent="$3" # If true, will check parent directory is writable if given path doesn't exist.

  if [[ "$check_type" == "provision" ]]; then
    test -w "$path" || \
    ($check_parent && test -w $(dirname "$path"))
  else
    test -w "$path" || ($check_parent && test -w $(dirname "$path"))
  fi

  update_result_json_with_rc "$test_type:$path" "$?"
}

check_free_space() {
  test_type="$1"
  path="$2"
  # check parent if path does not exist
  if [ ! -w "$path" ]; then
    path=$(dirname "$path")
  fi

  result=$(df -m $path 2>&1)

  if [ $? == "1" ]; then
    update_result_json_with_val_err "$test_type:$path" "$result" "1"
  else
    result=$(df -m $path | awk 'FNR == 2 {print $4}' 2>&1)
    update_result_json_with_val_err "$test_type:$path" "$result" $?
  fi
}

update_result_json_new() {
  # Input: test_name, check_passed
  if [[ -z "$result_kvs" ]]; then
    result_kvs="\"${1}\":{\"value\":\"${2}\",\"error\":\"${3}\"}"
  else
    result_kvs="${result_kvs},\"${1}\":{\"value\":\"${2}\",\"error\":\"${3}\"}"
  fi
}

update_result_json_with_val_err(){
  if [ $3 -eq 0 ]; then
    update_result_json_new "$1" "$2" "none"
  else
    update_result_json_new "$1" "none" "$2"
  fi
}

update_result_json() {
  # Input: test_name, check_passed
  if [[ -z "$result_kvs" ]]; then
    result_kvs="\"${1}\":\"${2}\""
  else
    result_kvs="${result_kvs},\"${1}\":\"${2}\""
  fi
}

update_result_json_with_rc() {
  # Input: test_name, returncode
  check_passed=false
  if [[ "$2" == "0" ]]; then
    check_passed=true
  fi
  update_result_json_new "$1" "$check_passed" "none"
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
  --ports_to_check PORTS_TO_CHECK
    Comma-separated list of ports to check availability
  --cleanup
    Deletes this script after being run. Allows `scp` commands to port over new preflight scripts.
  -h, --help
    Show usage.
EOT
}

err_msg() {
  echo $@ >&2
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
    --ports_to_check)
      ports_to_check="$2"
      shift
    ;;
    --yb_home_dir)
      yb_home_dir="$2"
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

if [[ "$check_type" == "provision" ]]; then
  preflight_provision_check >/dev/null 2>&1
else
  preflight_configure_check >/dev/null 2>&1
fi

echo "{$result_kvs}"
