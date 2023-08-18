---
title: Default ports reference
headerTitle: Default ports
linkTitle: Default ports
description: Default ports for YugabyteDB including client APIs, RPC communication, and monitoring.
menu:
  v2.14:
    identifier: default-ports
    parent: configuration
    weight: 2740
type: docs
---

## Client APIs

Application clients connect to the following addresses:

| API     | Port  | Server | Flag (default)           |
| ------- | ----- | ------- |------------------------------------------|
| ysql    | 5433  | yb-tserver | [`--pgsql_proxy_bind_address 0.0.0.0:5433`](../yb-tserver/#pgsql-proxy-bind-address) |
| ycql    | 9042  | yb-tserver | [`--cql_proxy_bind_address 0.0.0.0:9042`](../yb-tserver/#cql-proxy-bind-address)   |
| yedis   | 6379  | yb-tserver | [`--redis_proxy_bind_address 0.0.0.0:6379`](../yb-tserver/#redis-proxy-bind-address) |

## Internode RPC communication

Internode (server-to-server or node-to-node) communication is managed using RPC calls on the following addresses:

| Server    | Port | Flag (default)                              |
| ---------- | ---- | ------------------------------------------------------------ |
| yb-master  | 7100 |  [`--rpc_bind_addresses 0.0.0.0:7100`](../yb-master/#rpc-bind-addresses) |
| yb-tserver | 9100 |  [`--rpc_bind_addresses 0.0.0.0:9100`](../yb-tserver/#rpc-bind-addresses)<br/>[`--tserver_master_addrs 0.0.0.0:7100`](../yb-tserver/#tserver-master-addrs)<br/>[`--server_broadcast_addresses 0.0.0.0:9100`](../yb-tserver/#server-broadcast-addresses) |

To enable login to the machines running these servers, the SSH port 22 should be opened.

## Admin web server

Admin web server UI can be viewed at the following addresses:

| Server    | Port  | Flag (default)                             |
| ---------- | ----- | ------------------------------------------------------------ |
| yb-master  | 7000  |  [`--webserver_interface 0.0.0.0`](../yb-master/#webserver-interface)<br>[`--webserver_port 7000`](../yb-master/#webserver-port) |
| yb-tserver | 9000  |  [`--webserver_interface 0.0.0.0`](../yb-tserver/#webserver-interface)<br>[`--webserver_port 9000`](../yb-tserver/#webserver-port) |

## Firewall Rules

The following common ports are required for firewall rules:

| Service     | Port
| ------- | ------------------------- |
| SSH    | 22 |
| HTTP for YugabyteDB Anywhere  | 80 |
| HTTP for YugabyteDB Anywhere (alternate) | 8080 |
| HTTPS for YugabyteDB Anywhere  | 443 |
| HTTP for Replicated | 8800 |
| SSH  **   | 54422 |

** 54422 is a custom SSH port for universe nodes.

## Prometheus monitoring

YugabyteDB servers expose time series performance metrics in the [Prometheus exposition format](https://prometheus.io/docs/instrumenting/exposition_formats/#text-based-format) on multiple HTTP endpoints. These endpoints have the following structure.

```output
<target>/prometheus-metrics
```

You can access the Prometheus server on port `9090` of the YugabyteDB Anywhere node, and you can see the list of targets at the `http://<yugaware-ip>:9090/targets`. In particular, note port `9300` for node level metrics.

### Servers

Use the following targets to monitor `yb-tserver` and `yb-master` server metrics.

| Server     | Target                      |
| ---------- | --------------------------- |
| yb-master  | `<yb-master-address>:7000`  |
| yb-tserver | `<yb-tserver-address>:9000` |

### APIs

Use the following `yb-tserver` targets for the various API metrics.

| API     | Target
| ------- | ------------------------- |
| ysql    | `<yb-tserver-address>:13000` |
| ycql    | `<yb-tserver-address>:12000` |
| yedis   | `<yb-tserver-address>:11000` |

For a quick tutorial on using Prometheus with YugabyteDB, see [Observability with Prometheus](../../../explore/observability).
