#!/bin/bash
#set -euo pipefail
set -eu

ensure_option_has_arg() {
  if [[ $# -lt 2 ]]; then
    echo "Command line option $1 expects an argument" >&2
    exit 1
  fi
}

show_help() {
  cat >&2 <<-EOT
Usage: ${0##*/} --master_addresses <master-addresses> [--certs_dir_name <certs_dir_name>] \
[--bin_path <bin_path>] [--output_dir <output_dir>]
EOT
}


yb_build_args=( "$@" )

MASTER_ADDRS=""

BIN_PATH="/home/yugabyte/tserver/bin"

#CERTS_DIR_OPTION=" --certs_dir_name /home/yugabyte/yugabyte-tls-config/"
CERTS_DIR_OPTION=""

OUTPUT_DIR="/tmp"

while [[ $# -gt 0 ]]; do
  case ${1} in
    --master_addresses)
      MASTER_ADDRS="$2"
      shift
    ;;
    --certs_dir_dirname)
      CERTS_DIR_OPTION="--certs_dir_name $2"
      shift
    ;;
    --bin_path)
      BIN_PATH="$2"
      shift
    ;;
    --output_dir)
      OUTPUT_DIR="$2"
      shift
    ;;
    *)
      echo "Invalid option: '$1'" >&2
      exit 1
  esac
  shift
done

if [[ -z $MASTER_ADDRS ]]; then
    show_help
    exit 1
fi

YB_ADMIN_OPTIONS="--master_addresses=$MASTER_ADDRS $CERTS_DIR_OPTION"


# Find out all the TServers
${BIN_PATH}/yb-admin ${YB_ADMIN_OPTIONS} list_all_tablet_servers \
    > ${OUTPUT_DIR}/yb-admin-list_all_tablet_servers.txt

cat ${OUTPUT_DIR}/yb-admin-list_all_tablet_servers.txt \
    | awk '{ print $2 }' | grep ':' > ${OUTPUT_DIR}/ts-host_ports

# Query all the TServers and collect a list of tablets that retain delete markers.
for ts_host_port in `cat ${OUTPUT_DIR}/ts-host_ports`; do
    ${BIN_PATH}/yb-ts-cli ${CERTS_DIR_OPTION} --server_address=${ts_host_port} list_tablets \
        > ${OUTPUT_DIR}/ts-cli-list_tablets-${ts_host_port}.txt
done

for ts_host_port in `cat ${OUTPUT_DIR}/ts-host_ports`; do
 cat ${OUTPUT_DIR}/ts-cli-list_tablets-${ts_host_port}.txt \
    | grep -E "Tablet id:|Table name:|retain_delete_markers" \
    | grep -B 2 "retain_delete_markers: true" \
    | grep "Tablet id:" \
    | awk '{ print $NF }'
done | sort | uniq > ${OUTPUT_DIR}/tablets_retaining_deletes

echo "Tablets retaining deletes:"
cat ${OUTPUT_DIR}/tablets_retaining_deletes

# Optimization: exit early if ${OUTPUT_DIR}/tablets_retaining_deletes is empty
if [[ ! -s ${OUTPUT_DIR}/tablets_retaining_deletes ]]; then
  echo "No tablets are retaining delete markers"
  exit 0;
fi


# Find out all the index tables.
${BIN_PATH}/yb-admin ${YB_ADMIN_OPTIONS} list_tables \
            include_db_type include_table_id include_table_type \
    | tee ${OUTPUT_DIR}/yb-admin-list_tables.txt \
    | grep -E "index$" \
    | tee ${OUTPUT_DIR}/index_tables_details \
    | awk '{ print $2 }' > ${OUTPUT_DIR}/index_table_ids

# Gather all the index-infos from the master-dump
${BIN_PATH}/yb-admin ${YB_ADMIN_OPTIONS} dump_masters_state CONSOLE \
    > ${OUTPUT_DIR}/yb-admin-dump_masters_state

grep " schema " ${OUTPUT_DIR}/yb-admin-dump_masters_state \
    | sed 's/ indexes {/\n&/g' \
    | grep " indexes {" | sort | uniq > ${OUTPUT_DIR}/index_infos


# For each index
#  - find all the tablets in that table, and
#    INTERSECT with the ones retaining deletes to check if this table needs repair
#  - Also check the Index permission to verify that the backfill is indeed complete.
rm -f ${OUTPUT_DIR}/indexes_retaining_deletes_after_backfill.txt \
    ${OUTPUT_DIR}/indexes_still_doing_backfill.txt;

for index_table_id in `cat ${OUTPUT_DIR}/index_table_ids`; do
 details=`grep ${index_table_id} ${OUTPUT_DIR}/yb-admin-list_tables.txt`
 echo "Looking at ${details}";
 ${BIN_PATH}/yb-admin ${YB_ADMIN_OPTIONS} list_tablets tableid.${index_table_id} 0 \
     | grep -v "Tablet-UUID" | awk '{ print $1 }' > ${OUTPUT_DIR}/tablets-${index_table_id}

  # Check for intersection with tablets_retaining_deletes
  cat ${OUTPUT_DIR}/tablets_retaining_deletes ${OUTPUT_DIR}/tablets-${index_table_id} \
     | sort | uniq -c

  NUM_TABLETS_RETAINING_DELETES=`cat \
      ${OUTPUT_DIR}/tablets_retaining_deletes ${OUTPUT_DIR}/tablets-${index_table_id} \
     | sort | uniq -c | grep -v " 1 " | wc -l`

  INDEX_PERMISSION=`grep "indexes { table_id: \"${index_table_id}\"" ${OUTPUT_DIR}/index_infos \
                      | grep -o -E " index_permissions: [A-Z_]*" | awk '{ print $2 }'`

  if [[ $NUM_TABLETS_RETAINING_DELETES -ne 0 ]]; then
      if [[ "x$INDEX_PERMISSION" == "xINDEX_PERM_READ_WRITE_AND_DELETE" ]]; then
          echo "Index Table ${details} is retaining deletes, even after backfill is done.";
          echo ${details} >> ${OUTPUT_DIR}/indexes_retaining_deletes_after_backfill.txt
      else
          echo "Index Table ${details} is retaining deletes, " \
               "But this is OK, since  backfill is not yet done.";
          echo ${details} >> ${OUTPUT_DIR}/indexes_still_doing_backfill.txt
      fi
  else
      echo "Index Table ${details} is not retaining deletes."
  fi
done

if [[ -s  ${OUTPUT_DIR}/indexes_retaining_deletes_after_backfill.txt ]]; then
  exit -1
elif [[ -s  ${OUTPUT_DIR}/indexes_still_doing_backfill.txt  ]]; then
  exit -2
else # No indexes retaining deletes
  echo "No tablets are retaining delete markers"
  exit 0
fi
