---
title: Check servers
linkTitle: Check servers
headerTitle: Check YugabyteDB servers
description: How to check if your YugabyteDB servers are running
menu:
  preview:
    parent: troubleshoot-nodes
    weight: 10
type: docs
---

To troubleshoot server issues, you should perform a number of checks.

## Are the YugabyteDB servers running?

To verify whether or not the servers are running, you need to connect to the local node where YugabyteDB is running. On a local setup, this would be your local machine (or a Docker instance running on your local machine). On a multi-node cluster, you may need to `ssh` into the machines where the YugabyteDB nodes are running, as follows:

```sh
ps aux | grep yb-tserver
```

If you are expecting a YB-Master server on this node, execute the following command:

```sh
$ ps aux | grep yb-master
```

If the servers are not running, you can start them by using the following:

- [yugabyted](../../../reference/configuration/yugabyted/).

- [yb-tserver](../../../reference/configuration/yb-tserver/) and [yb-master](../../../reference/configuration/yb-master/) when using a [manual deployment](../../../deploy/manual-deployment/).

If the servers are running, yet they are not accessible from your client machine, it may indicate a network issue.

## Are the YB-Master and YB-TServer endpoints accessible?

Typically, the endpoints are defined as follows:

|   Description    |            URL            |
| ---------------- | ------------------------- |
| Master Web Page  | `<node-ip>:7000`          |
| TServer Web Page | `<node-ip>:9000`          |
| YSQL Metrics     | `<node-ip>:13000/metrics` |
| YCQL Metrics     | `<node-ip>:12000/metrics` |
| YSQL Server      | `<node-ip>:5433`          |
| YCQL Server      | `<node-ip>:9042`          |

However, in some cases these endpoints may not be accessible, depending on the configuration on your physical machines or on your cloud provider account.

### IP not accessible

Private versus Public IP: Consider setting up a VPN or using the node's public IP (for example, get it from the machine status on your cloud-provider account).

### Ports closed

- Cloud Account Configuration: Open the relevant ports, as per the following definition, for TCP traffic on your cloud provider account (for example, security group rules).

- Firewall is enabled: If your host has firewall enabled, run the following commands to open the ports using firewall exceptions.

  ```sh
  sudo firewall-cmd --zone=public --add-port=7000/tcp;
  sudo firewall-cmd --zone=public --add-port=7100/tcp;
  sudo firewall-cmd --zone=public --add-port=9000/tcp;
  sudo firewall-cmd --zone=public --add-port=9100/tcp;
  sudo firewall-cmd --zone=public --add-port=11000/tcp;
  sudo firewall-cmd --zone=public --add-port=12000/tcp;
  sudo firewall-cmd --zone=public --add-port=9300/tcp;
  sudo firewall-cmd --zone=public --add-port=9042/tcp;
  sudo firewall-cmd --zone=public --add-port=6379/tcp;
  ```

### Ports already in use

macOS Monterey enables AirPlay receiving by default, which listens on port 7000. This conflicts with YugabyteDB and causes `yugabyted start` to fail, as follows:

```sh
./bin/yugabyted start
```

```output
Starting yugabyted...
/ Running system checks...Failed to bind to address:  0.0.0.0:7000
```

The workaround is to disable AirPlay receiving, then start YugabyteDB, and then, optionally, enable AirPlay receiving. Alternatively, and this is the recommended approach, you can change the default port number using the `--master_webserver_port` flag when you start the cluster, as follows:

```sh
./bin/yugabyted start --master_webserver_port=9999
```
