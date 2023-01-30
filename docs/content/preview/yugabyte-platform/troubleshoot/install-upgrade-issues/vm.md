---
title: Troubleshoot install and upgrade issues on VMs
headerTitle: Troubleshoot install and upgrade issues on VMs
linkTitle: Install and upgrade issues
description: Troubleshoot issues encountered when installing or upgrading YugabyteDB Anywhere on VMs.
menu:
  preview_yugabyte-platform:
    identifier: install-upgrade-vm-issues
    parent: troubleshoot-yp
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../vm/" class="nav-link active">
      <i class="fa-solid fa-building"></i>
      VM
    </a>
  </li>

</ul>

You might encounter issues during installation and upgrade of YugabyteDB Anywhere.

If you experience difficulties while troubleshooting, contact [Yugabyte Support](https://support.yugabyte.com).

## Firewall enabled the YugabyteDB Anywhere host

If your host has firewall managed by firewalld enabled, then Docker Engine might not be able to connect to the host. To open the ports using firewall exceptions, execute the following command:

```sh
sudo firewall-cmd --zone=trusted --add-interface=docker0
sudo firewall-cmd --zone=public --add-port=80/tcp
sudo firewall-cmd --zone=public --add-port=443/tcp
sudo firewall-cmd --zone=public --add-port=8800/tcp
sudo firewall-cmd --zone=public --add-port=5432/tcp
sudo firewall-cmd --zone=public --add-port=9000/tcp
sudo firewall-cmd --zone=public --add-port=9090/tcp
sudo firewall-cmd --zone=public --add-port=32769/tcp
sudo firewall-cmd --zone=public --add-port=32770/tcp
sudo firewall-cmd --zone=public --add-port=9880/tcp
sudo firewall-cmd --zone=public --add-port=9874-9879/tcp
```


## Create mount paths on the nodes

You can create mount paths on the nodes with private IP addresses `10.1.13.150`, `10.1.13.151`, and `10.1.13.152` by executing the following command:

```sh
for IP in 10.1.12.103 10.1.12.104 10.1.12.105;
do
  ssh $IP mkdir -p /mnt/data0;
done
```

## Firewall enabled for nodes

You can add firewall exceptions on the nodes with private IP addresses `10.1.13.150`, `10.1.13.151`, and `10.1.13.152` by executing the following command:

```sh
for IP in 10.1.12.103 10.1.12.104 10.1.12.105;
do
  ssh $IP firewall-cmd --zone=public --add-port=7000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=7100/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9100/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=11000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=12000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9300/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9042/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=6379/tcp;
done
```


<!--

For YugabyteDB Anywhere HTTPS configuration, you should set your own key or certificate. If you do provide this setting, the default public key is used, creating a potential security risk.

-->
