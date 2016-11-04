#!/bin/sh
# Copyright (c) YugaByte, Inc.

#
# WARNING!!! This script needs a lot of cleanup still. But committing it since
# Amit and Hector want to stat using it,
#

set -euo pipefail

ALL='10.a.b.c1 10.a.b.c2 10.a.b.c2 10.a.b.c4'
MASTERS='10.a.b.c1 10.a.b.c2 10.a.b.c3'
TSERVERS=$ALL

PEMFILE=~/.ssh/id_rsa_test_clusters
PORT=54422

TAR=yb-server-0.0.1-SNAPSHOT.0c5fca01051baed142592cf79d937e3eb0e152b0

print_help() {
  cat <<EOT
Usage: ${0##*/} <command>
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

command=""
while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
    ;;
    *)
      command=$1
  esac
  shift
done

# Check if we have a valid command.
if [ -z "$command" ]; then
  echo "No valid command specified." >&2
  print_help
  exit 1
fi

case "$command" in
  masters_create)
    echo "Creating masters in cluster create mode..."
    for ip in $MASTERS; do
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh create &
    done
  ;;
  masters_start)
    echo "Starting masters..."
    for ip in $MASTERS; do
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh start &
    done
  ;;
  masters_stop)
    echo "Stopping masters..."
    for ip in $MASTERS; do
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh stop
      sleep 1
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh stop
      sleep 1
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh stop
      sleep 1
    done
  ;;
  masters_clean)
    echo "Cleaning masters..."
    for ip in $MASTERS; do
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh clean
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh clean-logs
    done
  ;;
  masters_rolling_upgrade|masters_rolling_restart)
    echo "Upgrading masters..."
    for ip in $MASTERS; do
      echo $ip
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh stop
      sleep 1
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh stop
      sleep 1
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh stop
      sleep 1
      if [[ $command == "masters_rolling_upgrade" ]]; then
        ssh -i $PEMFILE -p $PORT centos@$ip "sudo -u yugabyte rm /opt/yugabyte/master"
        ssh -i $PEMFILE -p $PORT centos@$ip "sudo -u yugabyte ln -s /opt/yugabyte/$TAR /opt/yugabyte/master"
      fi
      ssh -i $PEMFILE -p $PORT centos@$ip yb-master-ctl.sh start &
      sleep 1
      echo "Sleeping 5 seconds before going to next master...";
      sleep 5
    done
  ;;
  tservers_start)
    echo "Starting tservers..."
    for ip in $TSERVERS; do
      ssh -i $PEMFILE -p $PORT centos@$ip yb-tserver-ctl.sh start &
    done
  ;;
  tservers_stop)
    echo "Stopping tservers..."
    for ip in $TSERVERS; do
      ssh -i $PEMFILE -p $PORT centos@$ip yb-tserver-ctl.sh stop
      sleep 1
      ssh -i $PEMFILE -p $PORT centos@$ip yb-tserver-ctl.sh stop
      sleep 1
    done
  ;;
  tservers_clean)
    echo "Cleaning tservers..."
    for ip in $TSERVERS; do
      ssh -i $PEMFILE -p $PORT centos@$ip yb-tserver-ctl.sh clean
      ssh -i $PEMFILE -p $PORT centos@$ip yb-tserver-ctl.sh clean-logs
    done
  ;;
  tservers_rolling_upgrade|tservers_rolling_restart)
    echo "Upgrading tservers..."
    for ip in $TSERVERS; do
      echo $ip
      ssh -i $PEMFILE -p $PORT centos@$ip yb-tserver-ctl.sh stop
      sleep 1
      ssh -i $PEMFILE -p $PORT centos@$ip yb-tserver-ctl.sh stop
      sleep 1
      ssh -i $PEMFILE -p $PORT centos@$ip yb-tserver-ctl.sh stop
      sleep 1
      if [[ $command == "tservers_rolling_upgrade" ]]; then
        ssh -i $PEMFILE -p $PORT centos@$ip "sudo -u yugabyte rm /opt/yugabyte/tserver"
        ssh -i $PEMFILE -p $PORT centos@$ip "sudo -u yugabyte ln -s /opt/yugabyte/$TAR /opt/yugabyte/tserver"
      fi
      ssh -i $PEMFILE -p $PORT centos@$ip yb-tserver-ctl.sh start &
      sleep 1
      echo "Sleeping 45 seconds before going to next tserver...";
      sleep 45
    done
  ;;
  copy_tar)
    echo "Copying $TAR to all nodes, and untaring..."
    for ip in $ALL; do
      echo $ip
      scp -i $PEMFILE -P $PORT ~/code/yugabyte/build/$TAR.tar.gz centos@$ip:/tmp/.
      ssh -i $PEMFILE -p $PORT centos@$ip "sudo -u yugabyte mkdir -p /opt/yugabyte/$TAR"
      ssh -i $PEMFILE -p $PORT centos@$ip "(cd /opt/yugabyte/$TAR ; sudo -u yugabyte tar xvf /tmp/$TAR.tar.gz )"
    done
  ;;
  status)
    for ip in $ALL; do
      echo "Master related processes on $ip"
      ssh -i $PEMFILE -p $PORT centos@$ip "ps auxww | grep yb-master | grep -v bash | grep -v grep"
      echo "TServer related processes on $ip"
      ssh -i $PEMFILE -p $PORT centos@$ip "ps auxww | grep yb-tserver | grep -v bash | grep -v grep"
      echo "---"
    done
  ;;
  *)
    echo "Invalid command" >&2
    print_help
    exit 1
esac
