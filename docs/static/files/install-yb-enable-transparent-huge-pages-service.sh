#!/bin/bash

# Purpose: Utility which deploys enable-transparent-huge-pages.service
# Requirements: Must be run as root.  If not allowed, must do manually.
version=0.3
prog=$(basename "$0")

banner="${prog} [v${version}]"
prog_no_ext=$(basename "${prog%.*}")
logdir='./'
logfile=${prog_no_ext}.log
logfile_full_path=${logdir}${logfile}
pid=$$

err() {
    echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')] [${pid}] ERROR: $*" | tee -a "${logfile_full_path}" 1>&2
}

warn() {
    echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')] [${pid}] WARN: $*" | tee -a "${logfile_full_path}" 1>&2
}

info() {
    echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')] [${pid}] INFO: $*" | tee -a "${logfile_full_path}" 2>&1
}

usage="Usage: $0"
command_issued="$0 $@"
info "${banner} BEGIN"
info "Command issued: '${command_issued}'"
info "Logfile: '${logfile_full_path}'"

########## BEGIN Script Specific Variables
unit_filename="yb-enable-transparent-huge-pages.service"
unit_filepath="/etc/systemd/system/"
unit_file_full_path=${unit_filepath}${unit_filename}

unit_file_definition=$(cat <<EOF
[Unit]
Description=YugabyteDB Enable Transparent Hugepages (THP)
DefaultDependencies=no
After=local-fs.target
Before=sysinit.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'echo always | tee /sys/kernel/mm/transparent_hugepage/enabled > /dev/null && echo defer+madvise | tee /sys/kernel/mm/transparent_hugepage/defrag > /dev/null && echo 0 | tee /sys/kernel/mm/transparent_hugepage/khugepaged/max_ptes_none > /dev/null'

[Install]
WantedBy=basic.target
EOF
)

########## END Script Specific Variables

########## BEGIN Helper Functions

# Check that its enabled and has the desired settings
validate_transparent_hugepage_settings() {
    local settings=(
	"/sys/kernel/mm/transparent_hugepage/enabled [always]"
	"/sys/kernel/mm/transparent_hugepage/defrag [defer+madvise]"
	"/sys/kernel/mm/transparent_hugepage/khugepaged/max_ptes_none 0"
    )

    local valid=0
    local invalid=0

    for setting in "${settings[@]}"; do
	local path=$(echo "$setting" | awk '{print $1}')
	local expected_value=$(echo "$setting" | awk '{print $2}')

	if [[ ! -f "$path" ]]; then
	    error "$path: INVALID Does not exist!"
	    ((invalid++))
	    continue
	fi

	local actual_value=$(cat "$path")

	if [[ "$expected_value" == "0" && "$path" == *"max_ptes_none" ]]; then
	    if [[ "$actual_value" == "$expected_value" ]]; then
		info "$path: VALID (Expected: '$expected_value', Actual: '$actual_value')"
		((valid++))
	    else
		err "$path: INVALID (Expected: '$expected_value', Actual: '$actual_value')"
		((invalid++))
	    fi
	else
	    # Fuzzy Match for everything else
	    if [[ "$actual_value" == *"$expected_value"* ]]; then
		info "$path: VALID (Expected: '$expected_value', Actual: '$actual_value')"
		((valid++))
	    else
		err "$path: INVALID (Expected: '$expected_value', Actual: '$actual_value')"
		((invalid++))
	    fi
	fi

    done

    info "SUMMARY: Valid Checks: $valid , Invalid Checks: $invalid"

    if [[ "$valid" -eq 3 && "$invalid" -eq 0 ]]; then
	info "RESULT: All ${valid} settings applied."
	return 0
    else
	err "RESULT: ${invalid} setting(s) not applied."
	return 1
    fi
}

gather_info() {
    info "Gathering THP settings and counters:"
    info "BEGIN THP settings and counters"
    set -fx
    uname -a
    command -v hostnamectl && hostnamectl
    find /sys/kernel/mm/transparent_hugepage/ -type f -exec sh -c 'printf "%-70s %s\n" "{}" "$(cat {})"' \; \
	| tee -a "${logfile_full_path}" 2>&1
    set +fx
    info "END THP settings and counters"
}
########## END Helper Functions


########## BEGIN Business Logic
# Check the settings
info "Gatherg Information Before Changes"
gather_info
if validate_transparent_hugepage_settings; then
    info "THP Enabled with the desired settings."
else
    info "THP Does NOT have the desired settings."
fi


# Even if the file is there we intend to overwrite it.  Print out the
# information in the file before we overwrite it.

if [ -f "${unit_file_full_path}" ]; then
    info "${unit_file_full_path} already exists."
    info "BEGIN ${unit_file_full_path} Contents:"

    set -fx
    stat ${unit_file_full_path} | tee -a "${logfile_full_path}" 2>&1
    cat ${unit_file_full_path} | tee -a "${logfile_full_path}" 2>&1
    set +fx

    info "END ${unit_file_full_path} Contents:"
    info "Overwriting with ${version}"
else 
    info "${unit_file_full_path} does not exist, requires deployment."
    info "Creating ${unit_file_full_path} new"
fi

# Always perform this, because if we update settings, we always apply.
info "Configuring ${unit_file_full_path}"
set -fx
# echo "${unit_file_definition}" > ${unit_file_full_path} | tee -a "${logfile_full_path}" 2>&1
echo "${unit_file_definition}" | tee -a "${logfile_full_path}" 2>&1 > ${unit_file_full_path} 
set +fx

# Load the services
info "Loading and enabling service"

set -fx
systemctl daemon-reload | tee -a "${logfile_full_path}" 2>&1
systemctl enable ${unit_filename} | tee -a "${logfile_full_path}" 2>&1
systemctl start ${unit_filename} | tee -a "${logfile_full_path}" 2>&1
systemctl --no-pager status ${unit_filename} | tee -a "${logfile_full_path}" 2>&1
set +fx

#Check the service.  You have to check the actual exit code of the main.
exec_main_status=$(systemctl show enable-transparent-huge-pages.service --property=ExecMainStatus | cut -f2 -d=)
if [[ ${exec_main_status} -ne 0 ]]; then
   err "systemctl start ${unit_filename} encountered a non-zero exit status (Return: ${exec_main_status}). Exiting!"
   err "Check status output and logs of ${unit_file_full_path}"
   info "${banner} END"
   exit 1
fi
    
info "Enablement and loading done. Exiting."

# Print out all the info we care about, then validate all the desired settings
gather_info
if validate_transparent_hugepage_settings; then
    info "THP Enabled with the desired settings. Exiting with 0"
    info "IMPORTANT: In order for changes to take effect, restart master and tserver processses, or perform a rolling restart."
    info "${banner} END"
    exit 0
else
    err "THP not set with the appropriate values! Exiting with 1" 
    info "${banner} END"
    exit 1
fi

########## END Business Logic
