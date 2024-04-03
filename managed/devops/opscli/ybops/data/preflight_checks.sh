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
install_node_exporter=false
skip_ntp_check=false
mount_points=""
yb_home_dir="/home/yugabyte"
# This should be a comma separated key-value list. Associative arrays were add in bash 4.0 so
# they might not exist in the provided instance depending on how old it is.
result_kvs=""
YB_SUDO_PASS=""
ports_to_check=""
tmp_dir="/tmp"
PROMETHEUS_FREE_SPACE_MB=100
HOME_FREE_SPACE_MB=2048
VM_MAX_MAP_COUNT=262144
PYTHON_EXECUTABLES=('python3.6' 'python3' 'python3.7' 'python3.8' 'python')
LINUX_OS_NAME=""

set_linux_os_name() {
  LINUX_OS_NAME=$(awk -F= -v key="NAME" '$1==key {gsub(/"/, "", $2); print $2}'\
  /etc/os-release 2>/dev/null)
}

preflight_provision_check() {
  # Check python is installed.
  check_python

  # Check for internet access.
  if [[ "$airgap" = false ]]; then
    # Attempt to run "/dev/tcp" 3 times with a 3 second timeout and return success if any succeed.
    # --preserve-status flag will maintain the exit code of the "/dev/tcp" command.
    HOST="yugabyte.com"
    PORT=443

    for i in 1 2 3; do
      timeout 3 bash -c "cat < /dev/null > /dev/tcp/${HOST}/${PORT}" --preserve-status && break
    done
    update_result_json_with_rc "Internet Connection" "$?"
  fi

  # Check for yugabyte in AllowUsers in /etc/ssh/sshd_config
  if echo $YB_SUDO_PASS | sudo -S grep -q "AllowUsers" /etc/ssh/sshd_config; then
    echo $YB_SUDO_PASS | sudo -S egrep -q 'AllowUsers.* yugabyte( |@|$)' /etc/ssh/sshd_config
    update_result_json_with_rc "AllowUsers has yugabyte" "$?"
  fi

  if [[ $install_node_exporter = true ]]; then
    # Check node exporter isn't already installed.
    no_node_exporter=false
    if [[ "$(ps -ef | grep "node_exporter" | grep -v "grep" | grep -v "preflight" |
             wc -l | tr -d ' ')" = '0' ]]; then
      no_node_exporter=true
    fi
    update_result_json "(Prometheus) No Pre-existing Node Exporter Running" "$no_node_exporter"

    filepaths="/opt/prometheus /etc/prometheus /var/log/prometheus /var/run/prometheus \
      /var/lib/prometheus"
    # Check prometheus files are writable.
    if [[ $LINUX_OS_NAME = "SLES" ]]; then
      filepaths="$filepaths /usr/lib/systemd/system/node_exporter.service"
    else
      filepaths="$filepaths /lib/systemd/system/node_exporter.service"
    fi
    for path in $filepaths; do
      check_filepath "Prometheus" "$path" true
    done

    check_free_space "/opt/prometheus" $PROMETHEUS_FREE_SPACE_MB
    check_free_space $tmp_dir $PROMETHEUS_FREE_SPACE_MB # for downloading folder
  fi

  # Check ulimit settings.
  ulimit_filepath="/etc/security/limits.conf"
  check_filepath "PAM Limits" $ulimit_filepath true

  # Check NTP synchronization
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
      update_result_json "NTP time synchronization set up" true
    else
      update_result_json "NTP time synchronization set up" false
    fi
  fi

  # Check mount points are writeable.
  IFS="," read -ra mount_points_arr <<< "$mount_points"
  for path in "${mount_points_arr[@]}"; do
    check_filepath "Mount Point" "$path" false
  done

  # Check ports are available.
  IFS="," read -ra ports_to_check_arr <<< "$ports_to_check"
  for port in "${ports_to_check_arr[@]}"; do
    check_passed=true
    if echo $YB_SUDO_PASS | sudo -S netstat -tulpn | grep ":$port\s"; then
      check_passed=false
    fi
    update_result_json "Port $port is available" "$check_passed"
  done

  # Check yugabyte user belongs to yugabyte group if it exists.
  if id -u "yugabyte"; then
    yb_group=$(id -gn "yugabyte")
    user_status=false
    if [[ $yb_group == "yugabyte" ]]; then
      user_status=true
    fi
    update_result_json "Yugabyte User in Yugabyte Group" "$user_status"
  fi

  check_free_space "$yb_home_dir" $HOME_FREE_SPACE_MB

  # Check Locale
  result=$(locale -a | grep -q -E "en_US.utf8|en_US.UTF-8")
  if [[ "$?" -eq 0 ]]; then
    update_result_json "locale_present" true
  else
    update_result_json "locale_present" false
  fi

}

preflight_configure_check() {
  # Check yugabyte user exists.
  id -u yugabyte
  update_result_json_with_rc "Yugabyte User" "$?"

  # Check yugabyte user belongs to group yugabyte.
  yb_group=$(id -gn "yugabyte")
  user_status=false
  if [[ $yb_group == "yugabyte" ]]; then
    user_status=true
  fi
  update_result_json "Yugabyte Group" "$user_status"

  # Check home directory exists.
  check_filepath "Home Directory" "$yb_home_dir" false

  # Check virtual memory max map limit.
  vm_max_map_count=$(cat /proc/sys/vm/max_map_count 2> /dev/null)
  test ${vm_max_map_count:-0} -ge $VM_MAX_MAP_COUNT
  update_result_json_with_rc "vm_max_map_count" "$?"

  # Check for chronyc utility.
  command -v chronyc >/dev/null 2>&1
  update_result_json_with_rc "chronyc_installed" "$?"
}

# Checks for an available python executable
check_python() {
  python_status=false
  for py_executable in "${PYTHON_EXECUTABLES[@]}"; do
    if echo $YB_SUDO_PASS | sudo -S /bin/sh -c "/usr/bin/env $py_executable --version"; then
      python_status=true
    fi
  done
  update_result_json "Sudo Access to Python" "$python_status"
}

# Checks if given filepath is writable.
check_filepath() {
  test_type="$1"
  path="$2"
  check_parent="$3" # If true, will check parent directory is writable if given path doesn't exist.

  if [[ "$check_type" == "provision" ]]; then
    # Use sudo command for provision.
    echo $YB_SUDO_PASS | sudo -S test -w "$path" || \
    ($check_parent && echo $YB_SUDO_PASS | sudo -S test -w $(dirname "$path"))
  else
    test -w "$path" || ($check_parent && test -w $(dirname "$path"))
  fi

  update_result_json_with_rc "($test_type) $path is writable" "$?"
}

check_free_space() {
  path="$1"
  required_mb="$2"
  # check parent if path does not exist
  if [ ! -w "$path" ]; then
    path=$(dirname "$path")
  fi
  test $(echo $YB_SUDO_PASS | sudo -S df -m $path | awk 'FNR == 2 {print $4}') -gt $required_mb

  update_result_json_with_rc "$1 has free space of $required_mb MB $SPACE_STR" "$?"
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
  update_result_json "$1" "$check_passed"
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
  --sudo_pass_file
    Bash file containing the sudo password variable.
  --ports_to_check PORTS_TO_CHECK
    Comma-separated list of ports to check availability
  --tmp_dir TMP_DIRECTORY
    Tmp Directory on the specified node.
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
    --tmp_dir)
      tmp_dir="$2"
      shift
    ;;
    --sudo_pass_file)
      if [ -f $2 ]; then
        . $2
        rm -rf "$2"
      else
        err_msg "Failed to find sudo_pass_file: $2"
      fi
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

set_linux_os_name
if [[ "$check_type" == "provision" ]]; then
  preflight_provision_check >/dev/null 2>&1
else
  preflight_configure_check >/dev/null 2>&1
fi

echo "{$result_kvs}"
