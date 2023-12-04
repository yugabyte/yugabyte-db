#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

yb_ctl_destroy_create --rf 3
yb_ctl stop_node 1
yb_ctl stop_node 2
yb_ctl stop_node 3
yb_ctl stop_node 1 --master
yb_ctl start_node 1 --master
yb_ctl start_node 3
ysqlsh 3 -c ';'
ysqlsh 1 -c ';' && exit 1
ysqlsh 2 -c ';' && exit 1
yb_ctl restart_node 1
ysqlsh 1 -c ';'
ysqlsh 2 -c ';' && exit 1
yb_ctl restart_node 1
ysqlsh 1 -c ';'
ysqlsh 2 -c ';' && exit 1
yb_ctl restart_node 2 --master
ysqlsh 2 -c ';' && exit 1
yb_ctl start_node 2
ysqlsh 2 -c ';'
