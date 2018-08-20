---
title: Check Processes
linkTitle: Check Processes
description: Check YugaByte DB Processes
aliases:
  - /troubleshoot/nodes/check-processes/
menu:
  latest:
    parent: troubleshoot-nodes
    weight: 842
---

## 1. Are the YugaByte DB processes running?
Connect to the local node where YugaByte DB is running. 

On the local setup, this is your local machine (or a docker instance running on your local machine). On a multi-node cluster, you may need to `ssh` into the machines where the YugaByte node(s) are running.

```
ps aux | grep yb-tserver
```

If you are expecting a master process on this node you can also do: 

```
ps aux | grep yb-master
```

If the processes are not running you can start them with:
- `yb-ctl` when using a local cluster.

- `bin/yb-tserver` and `bin/yb-master` binaries when using a multi-node cluster.

Once the processes are running, if they are not accessible from your client machine this may be a network issue (see below).

## 2. Are the yb-master and yb-tserver endpoints accessible?
Generally the endpoints are: 

|      Description |                       URL |
|------------------|---------------------------|
| Master Web Page  | `<node-ip>:7000`          |
| TServer Web Page | `<node-ip>:9000`          |
| Redis Metrics    | `<node-ip>:11000/metrics` |
| CQL Metrics      | `<node-ip>:12000/metrics` |
| Redis Server     | `<node-ip>:6379`          |
| CQL Server       | `<node-ip>:9042`          |


However, in some setups these endpoints may not be accessible, depending on the configuration on your physical machines or on your cloud-provider account:

### IP not accessible: 
- Private vs Public IP: Consider setting up a VPN or using the node’s public IP (e.g. get it from the machine status on your cloud-provider account).

### Ports closed

- Cloud Account Configuration: Open the relevant ports (see below),  for TCP traffic on your cloud-provider account (e.g. security group rules).

- SELinux turned on: If your host has SELinux turned on, run the following commands to open the ports using firewall exceptions.

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