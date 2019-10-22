---
title: Default ports
linkTitle: Default ports
description: Default ports
menu:
  latest:
    identifier: default-ports
    parent: admin
    weight: 2405
isTocNested: true
showAsideToc: true
---


## RPC

| Service    | Port | Configuration setting (default)                              |
| ---------- | ---- | ------------------------------------------------------------ |
| yb-master  | 7100 | [`--rpc_bind_addresses=0.0.0.0:7100`](../yb-master/#rpc-bind-addresses) |
| yb-tserver | 9100 | [`--tserver_master_addrs=0.0.0.0:9100`](../yb-tserver/#tserver-master-addrs 0.0.0.0:7100)<br/>[`--server_broadcast_addresses=0.0.0.0:9100`](../yb-tserver/#server-broadcast-addresses) |

## Admin web server

| Service    | Port  | Configuration setting (default)                              |
| ---------- | ----- | ------------------------------------------------------------ |
| yb-master  | 7000  | [`--webserver_interface=0.0.0.0`](../yb-master/#webserver-interface)<br >[`--webserver_port=7000`](../yb-master/#webserver-port) |
| yb-tserver | 9000  | [`--webserver_interface=0.0.0.0`](../yb-master/#webserver-interface)<br >[`--webserver_port=9000`](../yb-master/#webserver-port) |
| ysql       | 13000 | `--pgsql_proxy_webserver_port 13000`                         |
| ycql       | 12000 | `--cql_proxy_webserver_port 12000`                           |
| yedis      | 11000 | `--redis_proxy_webserver_port 11000`                         |


## API interfaces

| Service | Port  | Configuration setting (default)           |
| ------- | ----- | ----------------------------------------- |
| ysql    | 5433  | `--pgsql_proxy_bind_address 0.0.0.0:5433` |
| ycql    | 9042  | `--cql_proxy_bind_address 0.0.0.0:9042`   |
| yedis   | 6379  | `--redis_proxy_bind_address 0.0.0.0:6379` |
