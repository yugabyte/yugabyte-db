---
title: Default ports reference
headerTitle: Default ports
linkTitle: Default ports
description: Default ports for YugabyteDB including client APIs, RPC communication, and monitoring.
menu:
  stable:
    identifier: default-ports
    parent: configuration
    weight: 3100
type: docs
---

## Client APIs

Application clients connect to the following addresses:

| API     | Port  | Server | Flag (default)           |
| ------- | ----- | ------- |------------------------------------------|
| YSQL | 5433  | YB-TServer | [--pgsql_proxy_bind_address 0.0.0.0:5433](../yb-tserver/#pgsql-proxy-bind-address) |
| YCQL | 9042  | YB-TServer | [--cql_proxy_bind_address 0.0.0.0:9042](../yb-tserver/#cql-proxy-bind-address)   |

## Internode RPC communication

Internode (server-to-server or node-to-node) communication, including xCluster, is managed using RPC calls on the following addresses:

| Server     | Port | Flag (default)                              |
| ---------- | ---- | ------------------------------------------------------------ |
| YB-Master  | 7100 | [--rpc_bind_addresses 0.0.0.0:7100](../yb-master/#rpc-bind-addresses) |
| YB-TServer | 9100 | [--rpc_bind_addresses 0.0.0.0:9100](../yb-tserver/#rpc-bind-addresses)<br/>[--tserver_master_addrs 0.0.0.0:7100](../yb-tserver/#tserver-master-addrs)<br/>[--server_broadcast_addresses 0.0.0.0:9100](../yb-tserver/#server-broadcast-addresses) |

To enable login to the machines running these servers, the SSH port 22 should be opened.

xCluster uses the YB-Master port 7100 for the initial communication, and then uses the YB-TServer port 9100 to get data changes. Note that YugabyteDB Anywhere obtains the replication lag information using Prometheus metrics from YB-TServer at port 9000. If this port is closed, the xCluster replication is not affected, but YugabyteDB Anywhere would not be able to display the replication lag.

Before installing YugabyteDB or YugabyteDB Anywhere, or upgrading the YugabyteDB software on YugabyteDB Anywhere, the following ports must be open on all YugabyteDB nodes, and be reachable from YugabyteDB Anywhere nodes:

| Service       | Port  |
| ------------- | ----- |
| YB Controller | 18018 |
| [Node agent](../../../yugabyte-platform/prepare/server-nodes-software/software-on-prem-manual/#install-node-agent) | 9070 |

For more information on networking in YugabyteDB Anywhere, refer to [Networking](../../../yugabyte-platform/prepare/networking/).

## Admin web server

Admin web server UI can be viewed at the following addresses:

| Server    | Port  | Flag (default)                             |
| ---------- | ----- | ------------------------------------------------------------ |
| YB-Master  | 7000  |  [--webserver_interface 0.0.0.0](../yb-master/#webserver-interface)<br>[--webserver_port 7000](../yb-master/#webserver-port) |
| YB-TServer | 9000  |  [--webserver_interface 0.0.0.0](../yb-tserver/#webserver-interface)<br>[--webserver_port 9000](../yb-tserver/#webserver-port) |

For clusters started using [yugabyted](../yugabyted/), the YugabyteDB UI can be viewed at the following address:

| Server        | Port  | Flag                                             |
| ------------- | ----- | ------------------------------------------------ |
| YugabyteDB UI | 15433 | [--ui](../yugabyted/#start)  (default is true) |

## Firewall rules

The following common ports are required for firewall rules:

| Service     | Port |
| ------- | ------------------------- |
| SSH    | 22 |
| HTTP for YugabyteDB Anywhere  | 80 |
| HTTP for YugabyteDB Anywhere (alternate) | 8080 |
| HTTPS for YugabyteDB Anywhere  | 443 |
| HTTP for Replicated | 8800 |
| Custom SSH port for universe nodes | 54422 |

### Firewall changes for CIS hardened images

Running YugabyteDB on CIS hardened RHEL 8 or 9 requires the following changes to the firewall:

```sh
#!/bin/bash

sudo dnf repolist
sudo dnf config-manager --set-enabled extras
sudo dnf install -y firewalld
sudo systemctl start firewalld

ports=(5433 9042 7100 9100 18018 9070 7000 9000 12000 13000 15433)

for port in "${ports[@]}"; do
   sudo firewall-cmd --zone=public --add-port=${port}/tcp --permanent
done

sudo firewall-cmd --reload
```

If you have customized any port settings, be sure to replace the port numbers as appropriate.

## Prometheus monitoring

YugabyteDB servers expose time series performance metrics in the [Prometheus exposition format](https://prometheus.io/docs/instrumenting/exposition_formats/#text-based-format) on multiple HTTP endpoints that have the following structure:

```output
<target>/prometheus-metrics
```

You can access the Prometheus server on port 9090 of the YugabyteDB Anywhere node, and you can see the list of targets at `http://<yugaware-ip>:9090/targets`. In particular, note port 9300 for node-level metrics:

| Service           | Port |
| ----------------- | ---- |
| Prometheus server for YugabyteDB Anywhere | 9090 |
| Node Exporter     | 9300 |

For information on using Prometheus with YugabyteDB, see [Observability with Prometheus](../../../explore/observability).

### Servers

Use the following targets to monitor YB-TServer and YB-Master server metrics:

| Server     | Target                      |
| ---------- | --------------------------- |
| YB-Master  | `<yb-master-address>:7000`  |
| YB-TServer | `<yb-tserver-address>:9000` |
| YugabyteDB UI | `<yb-tserver-address>:15433` |

### APIs

Use the following YB-TServer targets for the various API metrics:

| API     | Target |
| ------- | ------------------------- |
| YSQL    | `<yb-tserver-address>:13000` |
| YCQL    | `<yb-tserver-address>:12000` |
