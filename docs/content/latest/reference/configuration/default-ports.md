---
title: Default ports
linkTitle: Default ports
description: Default ports reference
section: REFERENCE
menu:
  latest:
    identifier: default-ports
    parent: configuration
    weight: 2740
aliases:
  - reference/default-ports
---


## RPC

| Service    | Port | Configuration setting (default)                              |
| ---------- | ---- | ------------------------------------------------------------ |
| yb-master  | 7100 | yb-master: [`--rpc_bind_addresses 0.0.0.0:7100`](../yb-master/#rpc-bind-addresses) |
| yb-tserver | 9100 | yb-tserver: [`--tserver_master_addrs 0.0.0.0:9100`](../yb-tserver/#tserver-master-addrs)<br/>yb-tserver: [`--server_broadcast_addresses=0.0.0.0:9100`](../yb-tserver/#server-broadcast-addresses) |

## Admin web server

| Service    | Port  | Configuration setting (default)                             |
| ---------- | ----- | ------------------------------------------------------------ |
| yb-master  | 7000  | yb-master: [`--webserver_interface=0.0.0.0`](../yb-master/#webserver-interface)<br >yb-master: [`--webserver_port 7000`](../yb-master/#webserver-port) |
| yb-tserver | 9000  | yb-tserver: [`--webserver_interface=0.0.0.0`](../yb-master/#webserver-interface)<br >yb-tserver: [`--webserver_port 9000`](../yb-master/#webserver-port) |
| ysql       | 13000 | yb-tserver: [`--pgsql_proxy_webserver_port 13000`](../yb-tserver/#pgsql-proxy-webserver-port)                         |
| ycql       | 12000 | yb-tserver: [`--cql_proxy_webserver_port 12000`](../yb-tserver/#cql-proxy-webserver-port)                           |
| yedis      | 11000 | yb-tserver: [`--redis_proxy_webserver_port 11000`](../yb-tserver/#redis-proxy-webserver-port)                         |

## API interfaces

| API     | Port  | Configuration setting (default)           |
| ------- | ----- | ----------------------------------------- |
| ysql    | 5433  | yb-tserver: [`--pgsql_proxy_bind_address 0.0.0.0:5433`](../yb-tserver/#pgsql-proxy-bind-address) |
| ycql    | 9042  | yb-tserver: [`--cql_proxy_bind_address 0.0.0.0:9042`](../yb-tserver/#cql-proxy-bind-address)   |
| yedis   | 6379  | yb-tserver: [`--redis_proxy_bind_address 0.0.0.0:6379`](../yb-tserver/#redis-proxy-bind-address) |
