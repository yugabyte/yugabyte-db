---
title: Uninstall YugabyteDB Anywhere software
headerTitle: Uninstall the YugabyteDB Anywhere software
linkTitle: Uninstall software
description: Uninstall the YugabyteDB Anywhere software.
menu:
  preview_yugabyte-platform:
    identifier: uninstall-software
    parent: administer-yugabyte-platform
    weight: 180
type: docs
---

## Remove YugabyteDB components from nodes

As described in [Eliminate an unresponsive node](../../manage-deployments/remove-nodes/), when a node enters an undesirable state, you can delete the node, with YugabyteDB Anywhere clearing up all the remaining artifacts except the `prometheus` and `yugabyte` user.

You can manually remove Yugabyte components from existing server images. Before attempting this, you have to determine whether or not YugabyteDB Anywhere is operational. If it is, you either need to delete the universe or delete the nodes from the universe.

## Uninstall YugabyteDB Anywhere

If you used YBA Installer to install YugabyteDB Anywhere, you can use the `clean` command to uninstall the software. This removes the YugabyteDB Anywhere software, but keeps any data such as PostgreSQL or Prometheus information. Refer to [Clean](../../install-yugabyte-platform/install-software/installer/#clean-uninstall).

To completely eliminate all traces of YugabyteDB Anywhere and configuration, you should consider reinstalling the operating system image (or rolling back to a previous image, if available).

## Delete on-premises database server nodes

You can remove YugabyteDB components and configuration from on-premises provider database server nodes as follows:

1. Log in to the server node as the `yugabyte` user.

1. Navigate to the `/home/yugabyte/bin` directory that contains a number of scripts including `yb-server-ctl.sh`. The arguments set in this script allow you to perform various functions on the YugabyteDB processes running on the node.

1. Execute the following command:

    ```shell
    ./bin/yb-server-ctl.sh clean-instance
    ```

This removes all YugabyteDB code and settings from the node, removing it from the Universe.

If you cannot find the `bin` directory, it means YugabyteDB Anywhere already cleared it during a successful deletion of the universe.

You should also erase the data from the volume mounted under the `/data` subdirectory, unless this volume is to be permanently erased by the underlying storage subsystem when the volume is deleted.

To erase this data, execute the following commands using any user with access to sudo:

```sh
sudo umount /data
```

```sh
sudo dd if=/dev/zero of=/dev/sdb bs=1M
```

The preceding commands assume the data volume is attached to the server as `/dev/sdb`.

If there is a requirement to remove the `yugabyte` user, execute the following command:

```sh
sudo userdel -r yugabyte
```

If there is a requirement to remove the `prometheus` user, execute the following command:

```sh
sudo rm -rf /opt/prometheus
```

You may now choose to reverse the system settings that you configured in [Provision nodes manually](../../prepare/server-nodes-software/software-on-prem-manual/).

## Delete YugabyteDB Anywhere from the server

To remove YugabyteDB Anywhere and Replicated components from the host server, execute the following commands as the `root` user (or prepend `sudo` to each command):

```sh
systemctl stop replicated replicated-ui replicated-operator
service replicated stop
service replicated-ui stop
service replicated-operator stop
docker stop replicated-premkit
docker stop replicated-statsd
```

```sh
docker rm -f replicated replicated-ui replicated-operator replicated-premkit replicated-statsd retraced-api retraced-processor retraced-cron retraced-nsqd retraced-postgres
```

```sh
docker images | grep "quay.io/replicated" | awk '{print $3}' | xargs sudo docker rmi -f
```

```sh
docker images | grep "registry.replicated.com/library/retraced" | awk '{print $3}' | xargs sudo docker rmi -f
```

```sh
yum remove -y replicated replicated-ui replicated-operator
```

```sh
rm -rf /var/lib/replicated* /etc/replicated* /etc/init/replicated*  /etc/default/replicated* /etc/systemd/system/replicated* /etc/sysconfig/replicated* /etc/systemd/system/multi-user.target.wants/replicated* /run/replicated*
```

```sh
rpm -qa | grep -i docker
yum remove docker-ce
rpm -qa | grep -i docker
yum remove docker-ce-cli
```

Finally, execute the following commands to delete the `/opt/yugabyte` directory on the node to prevent failure if later you decide to install YugabyteDB Anywhere on a node that was previously removed using the preceding instructions:

```sh
rm -rf /var/lib/containerd
rm -rf /home/replicated
rm -rf /opt/containerd
rm -rf /opt/yugabyte
```

<!--

You can uninstall YugabyteDB Anywhere in the Kubernetes environments.

## Uninstall in Docker environments

You can stop and remove YugabyteDB Anywhere on Replicated, as follows:

1. Execute the following command to gain access to applications installed on Replicated:

    ```sh
    /usr/local/bin/replicated apps
    ```

2. To stop YugabyteDB Anywhere, execute the following command, replacing *appid* with the application ID of YugabyteDB Anywhere obtained from the preceding step:

    ```sh
    /usr/local/bin/replicated app <appid> stop
    ```

THE rm COMMAND IN STEP 3 DOESN'T WORK, AS PER DEV. THIS IS WHY THIS WHOLE SECTION IS BEING REMOVED FOR NOW

3. Remove YugabyteDB Anywhere, as follows:

    ```sh
    /usr/local/bin/replicated app <appid> rm
    ```

2. Remove all YugabyteDB Anywhere containers, as follows:

    ```sh
    sudo docker images | grep "yuga" | awk '{print $3}' | xargs docker rmi -f
    ```

3. Delete the mapped directory, as follows:

    ```sh
    sudo rm -rf /opt/yugabyte
    ```

6. Uninstall Replicated by following instructions provided in [Removing Replicated](https://help.replicated.com/docs/native/customer-installations/installing-via-script/#removing-replicated).
-->

## Uninstall in Kubernetes environments

You can uninstall YugabyteDB Anywhere in Kubernetes, as follows:

1. To remove YugabyteDB Anywhere, execute the following Helm command:

    ```sh
    helm uninstall yw-test -n yw-test
    ```

    `-n` option specifies the namespace scope for this request.

    You should see a message similar to the following, notifying you that the subject release has been removed:

    ```output
    release "yw-test" uninstalled
    ```

2. Execute the following command to remove the `yw-test` namespace:

    ```sh
    kubectl delete namespace yw-test
    ```

    You should see a message similar to the following:

    ```output
    namespace "yw-test" deleted
    ```
