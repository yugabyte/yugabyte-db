---
title: Default ports
linkTitle: Default ports
description: Default ports reference
section: REFERENCE
block_indexing: true
menu:
  v2.0:
    identifier: default-ports
    parent: configuration
    weight: 2740
isTocNested: 3
showAsideToc: true
---

## Client APIs

Application clients connect to these addresses.

| API     | Port  | Server | Configuration setting (default)           |
| ------- | ----- | ------- |------------------------------------------|
| ysql    | 5433  | yb-tserver | [`--pgsql_proxy_bind_address 0.0.0.0:5433`](../yb-tserver/#pgsql-proxy-bind-address) |
| ycql    | 9042  | yb-tserver | [`--cql_proxy_bind_address 0.0.0.0:9042`](../yb-tserver/#cql-proxy-bind-address)   |
| yedis   | 6379  | yb-tserver | [`--redis_proxy_bind_address 0.0.0.0:6379`](../yb-tserver/#redis-proxy-bind-address) |

## Monitoring with Prometheus

Use the following targets to configure [Prometheus](https://prometheus.io/) to scrape available metrics (in [Prometheus exposition format](https://prometheus.io/docs/instrumenting/exposition_formats/#text-based-format)) from the YugabyteDB HTTP endpoint:

```
<target>/prometheus-metrics
```

You can access the Prometheus server on port `9090` of the Platform node, and you can see the list of targets at the `http://<yugaware-ip>:9090/targets`. In particular, note port `9300` for node level metrics.

For a quick tutorial on using Prometheus with YugabyteDB, see [Observability with Prometheus](../../../explore/observability).

### Servers

Use the following targets to monitor `yb-tserver` and `yb-master` server metrics.

| Server     | Target                     |
| ---------- | -------------------------- |
| yb-master  | `<master-address>:7100` |  
| yb-tserver | `<tserver-address>:9100`   |

### APIs

Use the following `yb-tserver` targets to more API metrics.

| API     | Target
| ------- | ------------------------- |
| ysql    | `<tserver-address>:13000` |
| ycql    | `<tserver-address>:12000` |
| yedis   | `<tserver-address>:11000` |

## Internode RPC communication

Internode (server-to-server or node-to-node) communication is managed using RPC calls on these addresses.

| Server    | Port | Configuration setting (default)                              |
| ---------- | ---- | ------------------------------------------------------------ |
| yb-master  | 7100 |  [`--rpc_bind_addresses 0.0.0.0:7100`](../yb-master/#rpc-bind-addresses) |
| yb-tserver | 9100 |  [`--rpc_bind_addresses 0.0.0.0:9100`](../yb-tserver/#rpc-bind-addresses)<br/>[`--tserver_master_addrs 0.0.0.0:7100`](../yb-tserver/#tserver-master-addrs)<br/>[`--server_broadcast_addresses 0.0.0.0:9100`](../yb-tserver/#server-broadcast-addresses) |

## Admin web server

Admin web server UI can be viewed at these addresses.

| Server    | Port  | Configuration setting (default)                             |
| ---------- | ----- | ------------------------------------------------------------ |
| yb-master  | 7000  |  [`--webserver_interface 0.0.0.0`](../yb-master/#webserver-interface)<br >[`--webserver_port 7000`](../yb-master/#webserver-port) |
| yb-tserver | 9000  |  [`--webserver_interface 0.0.0.0`](../yb-master/#webserver-interface)<br >[`--webserver_port 9000`](../yb-master/#webserver-port) |

### Firewall Rules

Along with the above, include the following common ports in firewall rules. 

| Service     | Port
| ------- | ------------------------- |
| SSH    | 22 |
| HTTP for Platform  | 80 |
| HTTP for Platform (alternate) | 8080 |
| HTTPS for Platform  | 443 |
| HTTP for Replicated | 8800 |
