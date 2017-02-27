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

check_if_tar_specified() {
  if [ -z "$tar_prefix" ]; then
    echo "Please specify --tar_prefix option";
    exit 1
  fi
}

# Execute a command on $ip. $port & $pem_file must be specified as well.
ssh_cmd() {
  ssh -i $pem_file -p $port centos@$ip "$@"
}

# Function expects two arguments.
#
# $1 : Server type (e.g., master, tserver)
# $2 : Roll Type (e.g., upgrade, restart).
roll_servers() {
  local server_type=$1
  local roll_type=$2
  local server_ips=""
  local sleep_time_secs=0

  if [[ $server_type == "master" ]]; then
      sleep_time_secs=5
      server_ips=$master_ips
  elif [[ $server_type = "tserver" ]]; then
      sleep_time_secs=45
      server_ips=$tserver_ips
  else
    echo "Invalid server_type: $server_type" >& 2
    exit 1
  fi

  if [[ $roll_type == "upgrade" ]]; then
    echo "Upgrading yb-${server_type} software on servers one at a time..."
  else
    echo "Restarting yb-${server_type} on servers one at a time..."
  fi

  for ip in $server_ips; do
    echo "Host=$ip"
    ssh_cmd yb-${server_type}-ctl.sh stop
    sleep 1
    ssh_cmd yb-${server_type}-ctl.sh stop
    sleep 1
    ssh_cmd yb-${server_type}-ctl.sh stop
    sleep 1
    if [[ $roll_type == "upgrade" ]]; then
      check_if_tar_specified
      ssh_cmd "sudo -u yugabyte rm /opt/yugabyte/${server_type}"
      ssh_cmd "sudo -u yugabyte ln -s /opt/yugabyte/$tar_prefix /opt/yugabyte/${server_type}"
    fi
    ssh_cmd yb-${server_type}-ctl.sh start &
    sleep 1
    echo "Sleeping $sleep_time_secs seconds before going to next $server_type...";
    sleep $sleep_time_secs
  done
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
  status                   - Status of masters and servers.
  execute "..."            - Execute a command on all hosts.
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
      repo="$2"
      shift
    ;;
    masters_create|masters_start|masters_stop|masters_clean|masters_rolling_restart|masters_rolling_upgrade)
      set_cmd "$1"
    ;;
    tservers_start|tservers_stop|tservers_clean|tservers_rolling_restart|tservers_rolling_upgrade)
      set_cmd "$1"
    ;;
    copy_tar|status)
      set_cmd "$1"
    ;;
    execute)
      set_cmd "$1"
      execute_command="$2"
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
      ssh_cmd yb-master-ctl.sh create &
    done
  ;;
  masters_start)
    echo "Starting masters..."
    for ip in $master_ips; do
      ssh_cmd yb-master-ctl.sh start &
    done
  ;;
  masters_stop)
    echo "Stopping masters..."
    for ip in $master_ips; do
      echo $ip
      ssh_cmd yb-master-ctl.sh stop
      sleep 1
      ssh_cmd yb-master-ctl.sh stop
      sleep 1
      ssh_cmd yb-master-ctl.sh stop
      sleep 1
    done
  ;;
  masters_clean)
    echo "Cleaning masters..."
    for ip in $master_ips; do
      ssh_cmd yb-master-ctl.sh clean
      ssh_cmd yb-master-ctl.sh clean-logs
    done
  ;;
  masters_rolling_restart)
    roll_servers master restart
  ;;
  masters_rolling_upgrade)
    roll_servers master upgrade
  ;;
  tservers_start)
    echo "Starting tservers..."
    for ip in $tserver_ips; do
      ssh_cmd yb-tserver-ctl.sh start &
    done
  ;;
  tservers_stop)
    echo "Stopping tservers..."
    for ip in $tserver_ips; do
      echo $ip
      ssh_cmd yb-tserver-ctl.sh stop
      sleep 1
      ssh_cmd yb-tserver-ctl.sh stop
      sleep 1
    done
  ;;
  tservers_clean)
    echo "Cleaning tservers..."
    for ip in $tserver_ips; do
      ssh_cmd yb-tserver-ctl.sh clean
      ssh_cmd yb-tserver-ctl.sh clean-logs
    done
  ;;
  tservers_rolling_restart)
    roll_servers tserver restart
  ;;
  tservers_rolling_upgrade)
    roll_servers tserver upgrade
  ;;
  copy_tar)
    check_if_tar_specified
    echo "Copying $tar_prefix to all nodes, and untaring..."
    for ip in $all_servers; do
      echo $ip
      scp -i $pem_file -P $port $repo/build/$tar_prefix.tar.gz centos@$ip:/tmp/.
      ssh_cmd "sudo -u yugabyte mkdir -p /opt/yugabyte/$tar_prefix"
      ssh_cmd "(cd /opt/yugabyte/$tar_prefix ; \
          sudo -u yugabyte tar xvf /tmp/$tar_prefix.tar.gz;\
          sudo /opt/yugabyte/$tar_prefix/bin/post_install.sh  )" &
    done
    wait
  ;;
  status)
    for ip in $master_ips; do
      echo "Master related processes on $ip"
      ssh_cmd "ps auxww | grep [y]b-master | grep -v bash"
    done
    echo "---"
    for ip in $tserver_ips; do
      echo "TServer related processes on $ip"
      ssh_cmd "ps auxww | grep [y]b-tserver | grep -v bash"
    done
    echo "---"
  ;;
  execute)
    for ip in $all_servers; do
      echo "Host=$ip"
      ssh_cmd $execute_command
    done
  ;;
  *)
    echo "Invalid command" >&2
    print_help
    exit 1
esac
