#!/bin/sh
# Copyright (c) YugaByte, Inc.

#
# WARNING!!! This script needs a lot of cleanup still.
#

set_cmd() {
  if [ -n "$command" ] && [ "$command" != "$1" ]; then
    echo "More than one command specified: $command, $1" >&2
    exit 1
  fi
  command="$1"
}

print_help() {
  cat <<EOT
Usage: ${0##*/} <options> <command>
Options:
 --pem_file <filename>     - name of the pem file (default: ~/.ssh/id_rsa_test_clusters)
 --master_ips <ips>        - space separated IP of masters (e.g., '10.a.b.c 10.d.e.f 10.g.h.i')
 --tserver_ips <ips>       - space separated IP of tserver (e.g., '10.a.b.c 10.d.e.f 10.g.h.i')
 --port <port>             - ssh port (default: 54422)
 --repo <repo base>        - repository base (default: ~/code/yugabyte) used to pick up TAR file
 --tar_prefix <tar_prefix> - tar file prefix (e.g., yb-server-0.0.1-SNAPSHOT.0c5fca01051baed142592cf79d937e3eb0e152b0)
Commands:
  masters_create           - Start the YB master processes for the cluster in cluster create mode.
  masters_start            - Start the YB master processes in normal mode.
  masters_stop             - Stop the TB master processes.
  masters_clean            - Clean the master data and logs.
  masters_rolling_restart  - Restarts the masters in a rolling manner.
  masters_rolling_upgrade  - Upgrades the masters to the newly copied TAR in a rolling manner.
  tservers_start           - Start the YB tserver processes.
  tservers_stop            - Stop the TB teserver processes.
  tservers_clean           - Clean the tserver data and logs.
  tservers_rolling_restart - Restarts the tservers in a rolling manner.
  tservers_rolling_upgrade - Upgrades the tservers to the newly copied TAR in a rolling manner.
  copy_tar                 - Copy the tar file to all the nodes.
  status                   - Status of masters and servers
EOT
}

port=54422
repo=~/code/yugabyte
command=""
master_ips=""
tserver_ips=""
pem_file=""

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
    ;;
    --pem_file)
      pem_file="$2"
      shift
    ;;
    --master_ips)
      master_ips="$2"
      shift
    ;;
    --tserver_ips)
      tserver_ips="$2"
      shift
    ;;
    --port)
      port="$2"
      shift
    ;;
    --tar_prefix)
      tar_prefix="$2"
      shift
    ;;
    --repo)
      repo="2"
      shift
    ;;
    masters_create|masters_stop|masters_clean|masters_rolling_restart|masters_rolling_upgrade)
      set_cmd "$1"
      shift
    ;;
    tservers_start|tservers_stop|tservers_clean|tservers_rolling_restart|tservers_rolling_upgrade)
      set_cmd "$1"
      shift
    ;;
    copy_tar|status)
      set_cmd "$1"
      shift
    ;;
    *)
      echo "Invalid command line argument: $1" >& 2
      print_help
      exit 1
  esac
  shift
done

# Find the union of master & tablet server ips.

all_servers=`echo $master_ips $tserver_ips | sed 's/\s\+/\n/g' | sort | uniq`

# Check if we have a valid command.
if [ -z "$command" ]; then
  echo "No valid command specified." >&2
  print_help
  exit 1
fi

if [ -z "$master_ips" ]; then
  echo "Please specify --master_ips option";
  print_help
  exit 1
fi

if [ -z "$tserver_ips" ]; then
  echo "Please specify --tserver_ips option";
  print_help
  exit 1
fi

if [ -z "$pem_file" ]; then
  echo "Please specify --pem_file option";
  print_help
  exit 1
fi

case "$command" in
  masters_create)
    echo "Creating masters in cluster create mode..."
    for ip in $master_ips; do
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh create &
    done
  ;;
  masters_start)
    echo "Starting masters..."
    for ip in $master_ips; do
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh start &
    done
  ;;
  masters_stop)
    echo "Stopping masters..."
    for ip in $master_ips; do
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh stop
      sleep 1
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh stop
      sleep 1
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh stop
      sleep 1
    done
  ;;
  masters_clean)
    echo "Cleaning masters..."
    for ip in $master_ips; do
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh clean
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh clean-logs
    done
  ;;
  masters_rolling_upgrade|masters_rolling_restart)
    echo "Upgrading masters..."
    for ip in $master_ips; do
      echo $ip
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh stop
      sleep 1
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh stop
      sleep 1
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh stop
      sleep 1
      if [[ $command == "masters_rolling_upgrade" ]]; then
        ssh -i $pem_file -p $port centos@$ip "sudo -u yugabyte rm /opt/yugabyte/master"
        ssh -i $pem_file -p $port centos@$ip "sudo -u yugabyte ln -s /opt/yugabyte/$tar_prefix /opt/yugabyte/master"
      fi
      ssh -i $pem_file -p $port centos@$ip yb-master-ctl.sh start &
      sleep 1
      echo "Sleeping 5 seconds before going to next master...";
      sleep 5
    done
  ;;
  tservers_start)
    echo "Starting tservers..."
    for ip in $tserver_ips; do
      ssh -i $pem_file -p $port centos@$ip yb-tserver-ctl.sh start &
    done
  ;;
  tservers_stop)
    echo "Stopping tservers..."
    for ip in $tserver_ips; do
      ssh -i $pem_file -p $port centos@$ip yb-tserver-ctl.sh stop
      sleep 1
      ssh -i $pem_file -p $port centos@$ip yb-tserver-ctl.sh stop
      sleep 1
    done
  ;;
  tservers_clean)
    echo "Cleaning tservers..."
    for ip in $tserver_ips; do
      ssh -i $pem_file -p $port centos@$ip yb-tserver-ctl.sh clean
      ssh -i $pem_file -p $port centos@$ip yb-tserver-ctl.sh clean-logs
    done
  ;;
  tservers_rolling_upgrade|tservers_rolling_restart)
    echo "Upgrading tservers..."
    for ip in $tserver_ips; do
      echo $ip
      ssh -i $pem_file -p $port centos@$ip yb-tserver-ctl.sh stop
      sleep 1
      ssh -i $pem_file -p $port centos@$ip yb-tserver-ctl.sh stop
      sleep 1
      ssh -i $pem_file -p $port centos@$ip yb-tserver-ctl.sh stop
      sleep 1
      if [[ $command == "tservers_rolling_upgrade" ]]; then
        ssh -i $pem_file -p $port centos@$ip "sudo -u yugabyte rm /opt/yugabyte/tserver"
        ssh -i $pem_file -p $port centos@$ip "sudo -u yugabyte ln -s /opt/yugabyte/$tar_prefix /opt/yugabyte/tserver"
      fi
      ssh -i $pem_file -p $port centos@$ip yb-tserver-ctl.sh start &
      sleep 1
      echo "Sleeping 45 seconds before going to next tserver...";
      sleep 45
    done
  ;;
  copy_tar)
    echo "Copying $tar_prefix to all nodes, and untaring..."
    for ip in $all_servers; do
      echo $ip
      scp -i $pem_file -P $port $repo/build/$tar_prefix.tar.gz centos@$ip:/tmp/.
      ssh -i $pem_file -p $port centos@$ip "sudo -u yugabyte mkdir -p /opt/yugabyte/$tar_prefix"
      ssh -i $pem_file -p $port centos@$ip "(cd /opt/yugabyte/$tar_prefix ; sudo -u yugabyte tar xvf /tmp/$tar_prefix.tar.gz )"
    done
  ;;
  status)
    for ip in $master_ips; do
      echo "Master related processes on $ip"
      ssh -i $pem_file -p $port centos@$ip "ps auxww | grep yb-master | grep -v bash | grep -v grep"
    done
    echo "---"
    for ip in $tserver_ips; do
      echo "TServer related processes on $ip"
      ssh -i $pem_file -p $port centos@$ip "ps auxww | grep yb-tserver | grep -v bash | grep -v grep"
    done
    echo "---"
  ;;
  *)
    echo "Invalid command" >&2
    print_help
    exit 1
esac
