#!/bin/bash
#
# Function declarations needed for later
function printTable()
{
    local -r delimiter="${1}"
    local -r data="$(removeEmptyLines "${2}")"

    if [[ "${delimiter}" != '' && "$(isEmptyString "${data}")" = 'false' ]]
    then
        local -r numberOfLines="$(wc -l <<< "${data}")"

        if [[ "${numberOfLines}" -gt '0' ]]
        then
            local table=''
            local i=1

            for ((i = 1; i <= "${numberOfLines}"; i = i + 1))
            do
                local line=''
                line="$(sed "${i}q;d" <<< "${data}")"

                local numberOfColumns='0'
                numberOfColumns="$(awk -F "${delimiter}" '{print NF}' <<< "${line}")"

                # Add Line Delimiter

                if [[ "${i}" -eq '1' ]]
                then
                    table="${table}$(printf '%s#+' "$(repeatString '#+' "${numberOfColumns}")")"
                fi

                # Add Header Or Body

                table="${table}\n"

                local j=1

                for ((j = 1; j <= "${numberOfColumns}"; j = j + 1))
                do
                    table="${table}$(printf '#| %s' "$(cut -d "${delimiter}" -f "${j}" <<< "${line}")")"
                done

                table="${table}#|\n"

                # Add Line Delimiter

                if [[ "${i}" -eq '1' ]] || [[ "${numberOfLines}" -gt '1' && "${i}" -eq "${numberOfLines}" ]]
                then
                    table="${table}$(printf '%s#+' "$(repeatString '#+' "${numberOfColumns}")")"
                fi
            done

            if [[ "$(isEmptyString "${table}")" = 'false' ]]
            then
                echo -e "${table}" | column -s '#' -t | awk '/^\+/{gsub(" ", "-", $0)}1'
            fi
        fi
    fi
}

function removeEmptyLines()
{
    local -r content="${1}"

    echo -e "${content}" | sed '/^\s*$/d'
}

function repeatString()
{
    local -r string="${1}"
    local -r numberToRepeat="${2}"

    if [[ "${string}" != '' && "${numberToRepeat}" =~ ^[1-9][0-9]*$ ]]
    then
        local -r result="$(printf "%${numberToRepeat}s")"
        echo -e "${result// /${string}}"
    fi
}

function isEmptyString()
{
    local -r string="${1}"

    if [[ "$(trimString "${string}")" = '' ]]
    then
        echo 'true' && return 0
    fi

    echo 'false' && return 1
}

function trimString()
{
    local -r string="${1}"

    sed 's,^[[:blank:]]*,,' <<< "${string}" | sed 's,[[:blank:]]*$,,'
}

function cleanup()
{
  rm -f "$1"
}

function showhelp()
{
echo "determine_repl_latency.sh -m MASTER_IP1:PORT,MASTER_IP2:PORT,MASTER_IP3:PORT [ -c PATH_TO_SSL_CERTIFICATE ] (-k KEYSPACE -t TABLENAME | -a) [ -p PORT ] [ -r report ] [ -o output ] [ -u units ]"
echo "Options:"
echo "  -m | --master_addresses       Comma separated list of master_server ip addresses with optional port numbers [default 7100]"
echo "  -c | --certs_dir_name         Path to directory containing SSL certificates if TLS node-to-node encryption is enabled"
echo "  -k | --keyspace               Name of keyspace that contains the table that is being queried. YSQL keyspaces must be prefixed with ysql"
echo "  -t | --table_name             Name of table"
echo "  -a | --all_tables             Specify this flag instead of -k and -t if you want all tables in the universe included"
echo "  -p | --port                   Port number of tablet server metrics page [default 9000]"
echo "  -r | --report_type            Possible values: all,detail,summary [default all]"
echo "  -o | --output_type            Possible values: report,csv [default report]"
echo "  -u | --units                  Possible values: us (microseconds), ms (milliseconds) [default ms]"
echo "  -h | --help                   This message"
}
if [[ ! $@ =~ ^\-.+ ]]
then
   echo "Use determine_repl_latency.sh -h | --help to get help with this script."
   exit 2
fi
set -o errexit -o pipefail -o noclobber -o nounset
TSERVER_METRICS_PORT=9000
FILES_TO_CLEANUP=""
# Get the keyspace and table_name arguments
! getopt --test > /dev/null
if [[ ${PIPESTATUS[0]} -ne 4 ]]; then
    echo '`getopt --test` failed in this environment.'
    exit 1
fi
TEST_FOR_JQ=$(which jq)
if [[ $? != 0 ]]; then
   echo 'jq must be installed in this environment for this script to work.'
   exit 2
fi
YBADMIN=$(which yb-admin)
if [[ $? != 0 ]]; then
   echo 'yb-admin must be in the PATH for this script to work.'
   exit 2
fi
OPTIONS="m:c:k:t:p:r:o:u:avh"
LONGOPTS=master_addresses:,certs_dir_name:,keyspace:,table_name:,port:,report_type:,output_type:,units:,all_tables,verbose,help
! PARSED=$(getopt --options=$OPTIONS --longoptions=$LONGOPTS --name "$0" -- "$@")
if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
    exit 2
fi
eval set -- "$PARSED"
m=n c=n k=n v=n t=n p=n r=n o=n u=n a=n
# Get the master_addresses argument and a certs directory (if TLS is enabled)
while true; do
    case "$1" in
       -m|--master_addresses)
           m=y
           master_addrs=$2
           shift 2
           ;;
       -c|--certs_dir_name)
           c=y
           certs_dir_name=$2
           shift 2
           ;;
       -k|--keyspace)
           k=y
           keyspace=$2
           shift 2
           ;;
       -t|--table_name)
           t=y
           table_name=$2
           shift 2
           ;;
       -p|--port)
           p=y
           port_number=$2
           shift 2
           ;;
       -r|--report_type)
           r=y
           report_type=$2
           shift 2
           ;;
       -o|--output_type)
           o=y
           output_type=$2
           shift 2
           ;;
       -u|--units)
           u=y
           units_type=$2
           shift 2
           ;;
       -a|--all_tables)
           a=y
           all_tables='true'
           shift
           ;;
       -h|--help)
           showhelp
           exit 0
           ;;
       --)
           shift
           break
           ;;
    esac
done
if [[ ${a} == "n" ]]; then
   all_tables='false'
fi
if [[ ${m} == "n" ]]; then
   echo "Missing argument (-m | --master_addresses)"
   exit 2
fi
if [[ ${t} == "n" && "${all_tables}" == 'false' ]]; then
   echo "Missing argument (-t | --table_name)"
   exit 2
fi
if [[ ${k} == "n" && "${all_tables}" == 'false' ]]; then
   echo "Missing argument (-k | --keyspace)"
   exit 2
fi
if [[ ${o} == "y" ]]; then
   case "$output_type" in
       csv)
         ;;
       report)
         ;;
       *)
         echo "Unknown output_type ${output_type}. Must be report or csv"
         exit 2
         ;;
    esac
else
    output_type="report"
fi
if [[ ${r} == "y" ]]; then
   case "$report_type" in
       detail)
         ;;
       summary)
         ;;
       all)
         ;;
       *)
         echo "Unknown report_type ${report_type}. Must be all, detail, or summary"
         exit 2
         ;;
   esac
else
   report_type="all"
fi
if [[ ${u} == "y" ]]; then
   case "$units_type" in
       us)
         units_label="\U3bcs"
         ;;
       ms)
         units_label="ms"
         ;;
       *)
         echo "Unknown unit_type ${units_type}. Must be us or ms"
         exit 2
         ;;
   esac
else
   units_type="ms"
   units_label="ms"
fi
# Get a list of tservers based on an argument with master_addresses
opt_tls=""
if [[ ${c} == "y" ]]; then
   opt_tls="-certs_dir_name ${certs_dir_name}"
fi
if [[ ${p} == "y" ]]; then
   TSERVER_METRICS_PORT=$port_number
fi
# Get a list of all tablet_servers
TSERVER_LIST=$(${YBADMIN} -master_addresses ${master_addrs} ${opt_tls} list_all_tablet_servers 2>/dev/null | awk '$0 ~ /ALIVE/ { print $2 }')
if [[ $? != 0 ]]; then
   echo "Failed to get list of tablet servers"
   exit 4
fi
# Get a list of all tables
${YBADMIN} -master_addresses ${master_addrs} ${opt_tls} list_tables include_db_type include_table_id 2>/dev/null | awk '$1 !~ /ysql\.template[0-1]/ { printf("%s|%s\n",$1,$2) }' > /tmp/table_list.$$
if [[ $? != 0 ]]; then
   echo "Failed to get list of tables"
   exit 4
fi
declare -A table_list
while IFS=\| read tablename table_uuid
do
    table_list[$table_uuid]=$tablename
done < /tmp/table_list.$$
FILES_TO_CLEANUP+=" /tmp/table_list.$$"
trap "cleanup ${FILES_TO_CLEANUP}" EXIT INT
# Get a list of replicated table uuids
REPL_TABLEID_LIST=`${YBADMIN} -master_addresses ${master_addrs} ${opt_tls} list_cdc_streams 2>/dev/null | awk '$1 ~ /table_id:/ { gsub(/\"/,""); printf("%s\n",$2) }'`
# Validate if the provided keyspace and table is in the replicated list
valid_table_found='false'
for tableid in ${REPL_TABLEID_LIST}
do
    add_to_tablet_list='false'
    keypart=$(echo ${table_list[${tableid}]} | cut -d . -f 1-2 -)
    tabpart=$(echo ${table_list[${tableid}]} | cut -d . -f 3- -)
    if [[ "${all_tables}" == 'true' ]]; then
       add_to_tablet_list='true'
       valid_table_found='true'
    else
        if [[ "${keypart}.${tabpart}" == "${keyspace}.${table_name}" || "${keypart}.${tabpart}" == "ycql.${keyspace}.${table_name}" ]]; then
           valid_table_found='true'
           add_to_tablet_list='true'
        fi
    fi
    if [[ "${add_to_tablet_list}" == 'true' ]]; then
       # Get a list of tablets
       # column 1 should be the tablet_uuid
       # last column should be the current leader
       ${YBADMIN} -master_addresses ${master_addrs} ${opt_tls} list_tablets ${keypart} ${tabpart} 0 2>/dev/null | awk '$1 ~ /^[0-9,a-f]/ { printf("%s|%s\n",$1,$NF) }' >> /tmp/tablet_list.$$
       if [[ $? != 0 ]]; then
          echo "Failed to get tablet_list from ${master_addrs}"
          exit 4
       fi
    fi
done
if [[ "${valid_table_found}" != 'true' ]]; then
   echo "No valid replicated tables found"
   exit 5
fi
declare -A tablet_list
declare -A metric_array
declare -A total_drift
declare -A max_drift
declare -A tablet_count
declare -A sync_count
declare -A min_last_read
declare -A max_last_read
while IFS=\| read tablet_uuid tablet_ipaddr
do
    tablet_list[$tablet_uuid]=$tablet_ipaddr
done < /tmp/tablet_list.$$
FILES_TO_CLEANUP+=" /tmp/tablet_list.$$"
# For each tserver, get the metrics into a json document
report_time=$(date '+%s')
for tserver in ${TSERVER_LIST}
do
   tserver_ip_addr=$(echo ${tserver} | cut -d: -f 1)
   tserver_metrics_addr=http://${tserver_ip_addr}:$TSERVER_METRICS_PORT/metrics
   curl -s ${tserver_metrics_addr} -o /tmp/tserver_metrics_${tserver}.json &
   total_drift[${tserver}]=0
   max_drift[${tserver}]=0
   min_last_read[${tserver}]=0
   max_last_read[${tserver}]=0
   sync_count[${tserver}]=0
   FILES_TO_CLEANUP+=" /tmp/tserver_metrics_${tserver}.json"
done
wait
# Extract the hybrid_clock_hybrid_time from each json document
# Extract the metrics for each cdc server
for tserver in ${TSERVER_LIST}
do
   export tserver_ip=${tserver}
   if [[ ! -f /tmp/tserver_metrics_${tserver}.json ]]; then
      touch /tmp/tserver_metrics_${tserver}.json
   fi
   [[ -f /tmp/cdc_metrics_${tserver}.json ]] && rm -f /tmp/cdc_metrics_${tserver}.json
   [[ -f /tmp/tablet_metrics_${tserver}.json ]] && rm -f /tmp/tablet_metrics_${tserver}.json
   tablet_count[${tserver}]=0
   cat /tmp/tserver_metrics_${tserver}.json | jq '.[] | select(.type == "cdc") | [{ tablet_id: .attributes.tablet_id, metrics: .metrics[]}]' > /tmp/cdc_metrics_${tserver}.json
   cat /tmp/tserver_metrics_${tserver}.json | jq '.[] | select(.type == "tablet") | [{"table_id":.attributes.table_id,"tablet_id":.id, "metrics": .metrics[]}]' > /tmp/tablet_metrics_${tserver}.json
   cat /tmp/tablet_metrics_${tserver}.json | jq -c -r '.[] |  "\(.metrics.name + "_" + .tablet_id + "_" + "\(env.tserver_ip)")|\(.metrics.value)"' >> /tmp/cdc_metrics.$$
   cat /tmp/cdc_metrics_${tserver}.json | jq -c -r '.[] | select(.metrics.name | contains("last")) |  "\(.metrics.name + "_" + .tablet_id + "_" + "\(env.tserver_ip)")|\(.metrics.value)"' >> /tmp/cdc_metrics.$$
   cat /tmp/tserver_metrics_${tserver}.json | jq -c -r '.[] | select(.type == "server") |  .metrics[] | select(.name | contains("hybrid")) | "\(.name + "_" + "\(env.tserver_ip)")|\(.value)"' >> /tmp/cdc_metrics.$$
   FILES_TO_CLEANUP+=" /tmp/cdc_metrics_${tserver}.json /tmp/tablet_metrics_${tserver}.json"
done
FILES_TO_CLEANUP+=" /tmp/cdc_metrics.$$"
while IFS=\| read metric_name metric_value
do
   metric_array[$metric_name]=$metric_value
done < /tmp/cdc_metrics.$$
if [[ ${output_type} == "report" && (${report_type} == "all" || ${report_type} == "detailed") ]]; then
   if [[ ${all_tables} == 'true' ]]; then
      echo -e "Replication Latency Report for: All tables"
   else
      echo -e "Replication Latency Report for: ${keyspace}.${table_name}"
   fi
fi
echo -e "Tablet UUID,Current Leader,LastReadable OpId,LastCkpt OpId,Sync,Hybrid ClockTime,Last Read HybridTime,Potential Drift,Last Read Timestamp" > /tmp/report_$$.txt
for tablet in ${!tablet_list[*]}
do
   current_tserver=${tablet_list[$tablet]}
   mapindex=${tablet}_${current_tserver}
   if [[ -s /tmp/cdc_metrics_${current_tserver}.json ]]; then
     [[ "${metric_array[last_readable_opid_index_$mapindex]:-}" ]] && last_readable_opid_index_value=${metric_array[last_readable_opid_index_$mapindex]} || last_readable_opid_index_value=0
     [[ "${metric_array[last_checkpoint_opid_index_$mapindex]:-}" ]] && last_checkpoint_opid_index_value=${metric_array[last_checkpoint_opid_index_$mapindex]} || last_checkpoint_opid_index_value=0
     [[ "${metric_array[last_read_opid_index_$mapindex]:-}" ]] && last_read_opid_index_value=${metric_array[last_read_opid_index_$mapindex]} || last_read_opid_index_value=0
     [[ "${metric_array[last_read_hybridtime_$mapindex]:-}" ]] && last_read_hybridtime_value=${metric_array[last_read_hybridtime_$mapindex]} || last_read_hybridtime_value=0
     [[ "${metric_array[last_read_physicaltime_$mapindex]:-}" ]] && last_read_physicaltime_value=${metric_array[last_read_physicaltime_$mapindex]} || last_read_physicaltime_value=0
   else
     last_readable_opid_index_value=0
     last_checkpoint_opid_index_value=0
     last_read_opid_index_value=0
     last_read_hybridtime_value=0
     last_read_physicaltime_value=0
   fi
#   [[ ${metric_array[majority_done_ops_$mapindex]} -lt 0 ]] && majority_done_ops=$(( ${metric_array[majority_done_ops_$mapindex]} * -1 )) || majority_done_ops=${metric_array[majority_done_ops_$mapindex]}
#   current_position=${metric_array[raft_term_$mapindex]}.${majority_done_ops}
   if [[ ${last_readable_opid_index_value} == ${last_checkpoint_opid_index_value} ]]; then
      sync_indicator="SYNC"
   else
      sync_ops_count=$(( ${last_readable_opid_index_value} - ${last_checkpoint_opid_index_value}))
      sync_indicator="${sync_ops_count}"
   fi
   [[ "${metric_array[hybrid_clock_hybrid_time_${current_tserver}]:-}" ]] && hybrid_clock_hybrid_time_value=${metric_array[hybrid_clock_hybrid_time_${current_tserver}]} || hybrid_clock_hybrid_time_value=0
   last_read_timestamp_ms=$(( $last_read_physicaltime_value / 1000000 ))
   last_read_timestamp=$(date -d @${last_read_timestamp_ms})
   if test "${tablet_count[${current_tserver}]}"
   then
      tablet_count[${current_tserver}]=$(( tablet_count[${current_tserver}] + 1 ))
   else
      tablet_count[${current_tserver}]=1
   fi
   if test "${sync_count[${current_tserver}]}"
   then
      if [[ ${sync_indicator} == "SYNC" ]]; then
         sync_count[${current_tserver}]=$(( sync_count[${current_tserver}] + 1 ))
      fi
   else
      if [[ ${sync_indicator} == "SYNC" ]]; then
         sync_count[${current_tserver}]=1
      fi
   fi
   if [[ ${sync_indicator} == "SYNC" ]]; then
      potential_hybrid_drift=0
   else
      potential_hybrid_drift=$(( ($hybrid_clock_hybrid_time_value - $last_read_hybridtime_value) / 1000 ))
   fi
   total_drift[${current_tserver}]=$(( total_drift[${current_tserver}] + potential_hybrid_drift ))
   if [[ ${min_last_read[${current_tserver}]} -gt 0 ]]; then
     if [[ last_read_timestamp_ms -lt ${min_last_read[${current_tserver}]} ]]; then
        min_last_read[${current_tserver}]=${last_read_timestamp_ms}
     fi
   else
     min_last_read[${current_tserver}]=${last_read_timestamp_ms}
   fi
   if [[ ${max_last_read[${current_tserver}]} -gt 0 ]]; then
     if [[ last_read_timestamp_ms -gt ${max_last_read[${current_tserver}]} ]]; then
        max_last_read[${current_tserver}]=${last_read_timestamp_ms}
     fi
   else
     max_last_read[${current_tserver}]=${last_read_timestamp_ms}
   fi
   if [[ ${max_drift[${current_tserver}]} -gt 0 ]]; then
     if [[ potential_hybrid_drift -gt ${max_drift[${current_tserver}]} ]]; then
        max_drift[${current_tserver}]=${potential_hybrid_drift}
     fi
   else
     max_drift[${current_tserver}]=${potential_hybrid_drift}
   fi
   if [[ "${units_type}" == "ms" ]]; then
      tablet_drift=$(( $potential_hybrid_drift / 1000 ))
   else
      tablet_drift=${potential_hybrid_drift}
   fi
   echo -e "${tablet},${tablet_list[$tablet]},${last_readable_opid_index_value},${last_checkpoint_opid_index_value},${sync_indicator},${hybrid_clock_hybrid_time_value},${last_read_hybridtime_value},${tablet_drift}${units_label},${last_read_timestamp}" >> /tmp/report_$$.txt
done
echo -e "Node,Sync,Tablets,Average Drift,Max Drift,Min Timestamp,Max Timestamp" > /tmp/summary_report_$$.txt
for tserver in ${!tablet_count[*]}
do
   if [[ ${tablet_count[${tserver}]} -eq 0 ]]; then
      tablet_count[${tserver}]=-1
   fi
   total_sync=${sync_count[${tserver}]}
   total_tablets=${tablet_count[${tserver}]}
   if [[ "$units_type" == "ms" ]]; then
      avg_drift=$(( ( total_drift[$tserver] / tablet_count[${tserver}] ) / 1000 ))
      drift_max=$(( max_drift[$tserver] / 1000 ))
   else
      avg_drift=$(( total_drift[$tserver] / tablet_count[${tserver}] ))
      drift_max=${max_drift[${tserver}]}
   fi
   min_ts=$(date -d @${min_last_read[${tserver}]})
   max_ts=$(date -d @${max_last_read[${tserver}]})
   echo -e "${tserver},${total_sync},${total_tablets},${avg_drift}${units_label},${drift_max}${units_label},${min_ts},${max_ts}" >> /tmp/summary_report_$$.txt
done
if [[ ${output_type} == "report" ]]; then
   case "$report_type" in
      all)
        printTable ',' "$(cat /tmp/report_$$.txt)"
        echo -e "Node Summary"
        printTable ',' "$(cat /tmp/summary_report_$$.txt)"
        ;;
      detailed)
        printTable ',' "$(cat /tmp/report_$$.txt)"
        ;;
      summary)
        echo -e "Node Summary"
        printTable ',' "$(cat /tmp/summary_report_$$.txt)"
        ;;
   esac
else
   case "$report_type" in
      detailed)
         echo -n "/tmp/report_$$.txt"
         ;;
      summary)
         echo -n "/tmp/summary_report_$$.txt"
         ;;
      all)
         echo -n "/tmp/report_$$.txt"
         ;;
    esac
fi
#FILES_TO_CLEANUP+=" /tmp/summary_report_$$.txt"
rm -f $FILES_TO_CLEANUP
