---
title: Troubleshoot YugaByte DB
weight: 701
---

## SELinux turned on

If your host has SELinux turned on, run the following commands to open the ports using firewall exceptions.

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

## Check host resources 

```sh
$ sudo echo -n "CPUs: ";cat /proc/cpuinfo | grep processor | wc -l; echo -n "Mem: ";free -h | grep Mem | tr -s " " | cut -d" " -f 2; echo -n "Disk: "; df -h / | grep -v Filesystem'; 
CPUs: 72
Mem: 251G
Disk: /dev/sda2       160G   13G  148G   8% /
10.1.12.104
CPUs: 88
Mem: 251G
Disk: /dev/sda2       208G   22G  187G  11% /
10.1.12.105
CPUs: 88
Mem: 251G
Disk: /dev/sda2       208G  5.1G  203G   3% /
```

## Create mount paths

```sh
sudo mkdir -p /mnt/data0; 
```
