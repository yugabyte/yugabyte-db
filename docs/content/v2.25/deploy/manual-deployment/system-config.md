---
title: System configuration
headerTitle: 1. System configuration
linkTitle: 1. System configuration
description: How to configure system parameters to get the YugabyteDB database cluster to run correctly.
menu:
  v2.25:
    identifier: deploy-manual-deployment-system-config
    parent: deploy-manual-deployment
    weight: 611
type: docs
---

Perform the following configuration on each node in the cluster:

1. Set up time synchronization.
1. Set ulimits.
1. Enable transparent hugepages.

Keep in mind that, although YugabyteDB is PostgreSQL compatible and runs a PostgreSQL process, it is not a PostgreSQL distribution. The PostgreSQL it runs doesn't need the same OS and system resources that open source PostgreSQL requires. For this reason, the kernel configuration requirements are different.

In particular, the main YugabyteDB process, the YB-TServer, is multi-threaded. As a result, you don't need to modify settings for shared memory and inter-process communication (IPC), because there is no inter-process communication or shared memory in a multi-threaded process model (all memory is shared by the same process).

## Set up time synchronization

YugabyteDB relies on clock synchronization to guarantee consistency in distributed transactions. chrony is the preferred NTP implementation for clock synchronization.

### Install chrony

To install chrony, run:

```sh
$ sudo yum install -y chrony
```

### Configure Precision Time Protocol

{{<tags/feature/tp>}} Precision Time Protocol (PTP) is a network protocol designed for highly accurate time synchronization across devices in a network. PTP provides microsecond-level accuracy. PTP relies on a PTP Hardware Clock (PHC), a dedicated physical clock device that enhances time synchronization accuracy.

Currently, PTP is only available for AWS. To check if your AWS instance supports PTP and PHC, see [AWS PTP Hardware Clock](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/configure-ec2-ntp.html#connect-to-the-ptp-hardware-clock).

Configure PTP using the `configure_ptp.sh` script in the bin directory of your YugabyteDB home directory as follows:

```sh
sudo bash ./bin/configure_ptp.sh
```

### Configure ClockBound

{{<tags/feature/tp idea="1807">}} [ClockBound](https://github.com/aws/clock-bound) is an open source daemon that allows you to compare timestamps to determine order for events and transactions, independent of an instance's geographic location. ClockBound provides a strict interval in which the reference time (true time) exists.

Although optional, configuring ClockBound improves clock accuracy by several orders of magnitude. ClockBound requires chrony and can be used in conjunction with PTP.

ClockBound is supported on AWS and GCP. Azure is not supported.

Configure ClockBound using the `configure_clockbound.sh` script in the bin directory of your YugabyteDB home directory as follows:

```sh
sudo bash ./bin/configure_clockbound.sh
```

After configuring ClockBound, you must configure the YB-TServer and YB-Master servers with the `time_source=clockbound` flag.

If the ClockBound agent is configured with PTP, use a more aggressive clock error estimate such as `clockbound_clock_error_estimate_usec=100`.

#### Verify ClockBound configuration

Verify that ClockBound is configured properly using the following command:

```sh
systemctl status clockbound
```

A correctly configured ClockBound service reports no errors.  The following shows example output with PTP enabled:

```sh
● clockbound.service - ClockBound
     Loaded: loaded (/usr/lib/systemd/system/clockbound.service; enabled; preset: disabled)
     Active: active (running) since Wed 2024-10-16 23:49:38 UTC; 53s ago
   Main PID: 92765 (clockbound)
      Tasks: 3 (limit: 22143)
     Memory: 4.1M
        CPU: 18ms
     CGroup: /system.slice/clockbound.service
             └─92765 /usr/local/bin/clockbound --max-drift-rate 50 -r PHC0 -i eth0

Oct 16 23:49:38 ip-172-199-76-70.ec2.internal systemd[1]: Started ClockBound.
Oct 16 23:49:38 ip-172-199-76-70.ec2.internal clockbound[92765]: 2024-10-16T23:49:38.629593Z  INFO main ThreadId(01) /root/.cargo/registry/src/index.crates.io-6f17d22bba15001f/c>
Oct 16 23:49:38 ip-172-199-76-70.ec2.internal clockbound[92765]: 2024-10-16T23:49:38.629874Z  INFO ThreadId(02) /root/.cargo/registry/src/index.crates.io-6f17d22bba15001f/clock->
Oct 16 23:49:38 ip-172-199-76-70.ec2.internal clockbound[92765]: 2024-10-16T23:49:38.630045Z  INFO ThreadId(03) /root/.cargo/registry/src/index.crates.io-6f17d22bba15001f/clock->
```

## Set ulimits

In Linux, ulimit is used to limit and control the usage of system resources (threads, files, and network connections) on a per-process or per-user basis.

### Check ulimits

Run the following command to check the ulimit settings.

```sh
$ ulimit -a
```

The following settings are recommended when running YugabyteDB.

```output
core file size          (blocks, -c) unlimited
data seg size           (kbytes, -d) unlimited
scheduling priority             (-e) 0
file size               (blocks, -f) unlimited
pending signals                 (-i) 119934
max locked memory       (kbytes, -l) 64
max memory size         (kbytes, -m) unlimited
open files                      (-n) 1048576
pipe size            (512 bytes, -p) 8
POSIX message queues     (bytes, -q) 819200
real-time priority              (-r) 0
stack size              (kbytes, -s) 8192
cpu time               (seconds, -t) unlimited
max user processes              (-u) 12000
virtual memory          (kbytes, -v) unlimited
file locks                      (-x) unlimited
```

### Set system-wide ulimits

You can change values by substituting the `-n` option for any possible value in the output of `ulimit -a`. Issue a command in the following form to change a ulimit setting.

```sh
$ ulimit -n <value>
```

```sh
-f (file size): unlimited
-t (cpu time): unlimited
-v (virtual memory): unlimited [1]
-l (locked-in-memory size): unlimited
-n (open files): 64000
-m (memory size): unlimited [1] [2]
-u (processes/threads): 64000
```

{{< note title="Restart servers" >}}

If you change a ulimit setting on a node where the YB-Master and YB-TServer servers are already running, you must restart the servers for the new settings to take effect. Check the [yb-tserver.INFO](../start-masters/#verify-tserver-health) file to verify that the ulimits are applied. You can also check the `/proc/<process pid>` file to see the current settings.

{{< /note >}}

Changes made using ulimit may revert following a system restart depending on the system configuration. These settings should be applied permanently by adding the following in `/etc/security/limits.conf`:

```conf
*                -       core            unlimited
*                -       data            unlimited
*                -       fsize           unlimited
*                -       sigpending      119934
*                -       memlock         64
*                -       rss             unlimited
*                -       nofile          1048576
*                -       msgqueue        819200
*                -       stack           8192
*                -       cpu             unlimited
*                -       nproc           12000
*                -       locks           unlimited
```

On CentOS, `/etc/security/limits.d/20-nproc.conf` must also be configured to match.

```conf
*          soft    nproc     12000
```

After changing a ulimit setting in `/etc/security/limits.conf`, you will need to log out and back in. To update system processes, you may need to restart.

{{< note title="Using other distributions" >}}
If you're using a desktop-distribution, such as ubuntu-desktop, the preceding settings may not suffice. The operating system needs additional steps to change ulimit for GUI login.

In the case of ubuntu-desktop, in `/etc/systemd/user.conf` and `/etc/systemd/system.conf`, add `DefaultLimitNOFILE=64000` at the end of file.

Something similar may be needed for other distributions.
{{< /note >}}

### Kernel settings

If running on a virtual machine, execute the following to tune kernel settings:

1. Configure the parameter `vm.swappiness` as follows:

    ```sh
    sudo bash -c 'sysctl vm.swappiness=0 >> /etc/sysctl.conf'
    ```

1. Setup path for core files as follows:

    ```sh
    sudo sysctl kernel.core_pattern=/home/yugabyte/cores/core_%p_%t_%E
    ```

1. Configure the parameter `vm.max_map_count` as follows:

    ```sh
    sudo sysctl -w vm.max_map_count=262144
    sudo bash -c 'sysctl vm.max_map_count=262144 >> /etc/sysctl.conf'
    ```

1. Validate the change as follows:

    ```sh
    sysctl vm.max_map_count
    ```

### Using systemd

If you're using [systemd](https://systemd.io/) to start the processes, and the ulimits are not propagated, add the ulimits in the `Service` section of the systemd configuration file:

```output
[Unit]
.....

[Service]
.....
ulimits options here

[Install]
.....
```

For more details, see the systemd example configuration for [yb-master.service](/code-samples/yb-master.service), and [yb-tserver.service](/code-samples/yb-tserver.service).

The mappings of ulimit options with values are:

Data type | ulimit equivalent | Value |
----------|-------------|-----|
Directive       | ulimit equivalent    | Value |
LimitCPU=       | ulimit -t            | infinity |
LimitFSIZE=     | ulimit -f            | infinity |
LimitDATA=      | ulimit -d            | infinity |
LimitSTACK=     | ulimit -s            | 8388608 |
LimitCORE=      | ulimit -c            | infinity |
LimitRSS=       | ulimit -m            | infinity |
LimitNOFILE=    | ulimit -n            | 1048576  |
LimitAS=        | ulimit -v            | infinity |
LimitNPROC=     | ulimit -u            | 12000  |
LimitMEMLOCK=   | ulimit -l            | 64 |
LimitLOCKS=     | ulimit -x            | infinity |
LimitSIGPENDING=| ulimit -i            | 119934 |
LimitMSGQUEUE=  | ulimit -q            | 819200 |
LimitNICE=      | ulimit -e            | 0 |
LimitRTPRIO=    | ulimit -r            | 0 |

If a ulimit is set to `unlimited`, set it to `infinity` in the systemd configuration file.

## Enable transparent hugepages

Transparent hugepages should be enabled for optimal performance. By default, they are enabled.

You can check with the following command:

```sh
$ cat /sys/kernel/mm/transparent_hugepage/enabled
```

You should see the following output:

```output
[always] madvise never
```

In addition, you should verify that transparent hugepages use the following settings:

```output
/sys/kernel/mm/transparent_hugepage/defrag:
    always defer [defer+madvise] madvise never

/sys/kernel/mm/transparent_hugepage/khugepaged/max_ptes_none:
    0
```

If any of these values are not set as shown, you should modify your kernel command line to match. Consult your operating system documentation to determine the best way to modify a kernel command line argument for your operating system.

For example, on RHEL or CentOS 7 or 8, using grub2, you can use the following steps to enable transparent hugepages:

1. Append "transparent_hugepage=always" to `GRUB_CMDLINE_LINUX` in `/etc/default/grub`.

    ```sh
    GRUB_CMDLINE_LINUX="rd.lvm.lv=rhel/root rd.lvm.lv=rhel/swap ... transparent_hugepage=always"
    ```

1. Rebuild `/boot/grub2/grub.cfg` using grub2-mkconfig.

    Be sure to take a backup of the existing `/boot/grub2/grub.cfg` before rebuilding.

    On BIOS-based machines:

    ```sh
    grub2-mkconfig -o /boot/grub2/grub.cfg
    ```

    On UEFI-based machines:

    ```sh
    grub2-mkconfig -o /boot/efi/EFI/redhat/grub.cfg
    ```

1. Reboot the system.
