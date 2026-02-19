---
title: Uninstall YugabyteDB Anywhere software
headerTitle: Uninstall the YugabyteDB Anywhere software
linkTitle: Uninstall software
description: Uninstall the YugabyteDB Anywhere software.
menu:
  v2025.1_yugabyte-platform:
    identifier: uninstall-software
    parent: administer-yugabyte-platform
    weight: 180
type: docs
---

## Uninstall YugabyteDB Anywhere

Before uninstalling YugabyteDB Anywhere, you may want to [back up](../back-up-restore-yba/) configuration settings or data you want to keep.

Note that uninstalling YugabyteDB Anywhere removes the YugabyteDB Anywhere application, but does not automatically remove YugabyteDB universes. If YugabyteDB Anywhere is managing universes, delete them as needed before uninstalling the application.

If you used YBA Installer to install YugabyteDB Anywhere, you can use the `clean` command to uninstall the software. This removes the YugabyteDB Anywhere software, but keeps any data such as PostgreSQL or Prometheus information. Refer to [Clean](../../install-yugabyte-platform/install-software/installer/#clean-uninstall).

To completely eliminate all traces of YugabyteDB Anywhere and configuration, consider reinstalling the operating system image (or rolling back to a previous image, if available).

### Uninstall in Kubernetes environments

You can uninstall YugabyteDB Anywhere in Kubernetes, as follows:

1. To remove YugabyteDB Anywhere, run the following Helm command:

    ```sh
    helm uninstall <release-name> -n <namespace>
    ```

    Replace <release-name> and <namespace> with the release and namespace you used when installing. The `-n` option specifies the namespace scope for this request.

    You should see a message similar to the following, notifying you that the subject release has been removed:

    ```output
    release "<release-name>" uninstalled
    ```

1. Run the following command to remove the namespace:

    ```sh
    kubectl delete namespace <namespace>
    ```

    You should see a message similar to the following:

    ```output
    namespace "<namespace>" deleted
    ```

## Remove YugabyteDB components from nodes

As described in [Eliminate an unresponsive node](../../manage-deployments/remove-nodes/#eliminate-an-unresponsive-node), when a node enters an undesirable state, you can delete the node, with YugabyteDB Anywhere clearing up all the remaining artifacts except the `prometheus` and `yugabyte` user.

You can manually remove Yugabyte components from existing server images. Before attempting this, you have to determine whether or not YugabyteDB Anywhere is operational. If it is, you either need to delete the universe or delete the nodes from the universe.

### Delete on-premises database server nodes

You can remove YugabyteDB components and configuration from on-premises provider database server nodes as follows:

1. Log in to the server node as the `yugabyte` user.

1. Navigate to the `/home/yugabyte/bin` directory that contains a number of scripts including `yb-server-ctl.sh`. The arguments set in this script allow you to perform various functions on the YugabyteDB processes running on the node.

    If you cannot find the `bin` directory, it means YugabyteDB Anywhere already removed it during a successful deletion of the universe.

1. Stop YugabyteDB processes.

    For cron-based universes, run the following commands:

    ```sh
    ./bin/yb-server-ctl.sh master stop
    ./bin/yb-server-ctl.sh tserver stop
    ./bin/yb-server-ctl.sh controller stop
    ```

    For systemd universes (user-level services), run the following commands:

    ```sh
    systemctl --user stop yb-master
    systemctl --user stop yb-tserver
    systemctl --user stop yb-controller
    ```

    For systemd universes (system-level services), run the following commands:

    ```sh
    sudo systemctl stop yb-master
    sudo systemctl stop yb-tserver
    sudo systemctl stop yb-controller
    ```

1. If the on-premises nodes are not manually provisioned, depending on the VM image, for files in `/etc/systemd/system`, `/usr/lib/systemd/system`, or `</home/yugabyte | yb_home_dir>/.config/systemd/user`, do the following:

    ```sh
    rm <dir>/yb-clean_cores.timer
    rm <dir>/yb-clean_cores.service
    rm <dir>/yb-zip_purge_yb_logs.timer
    rm <dir>/yb-zip_purge_yb_logs.service
    rm <dir>/yb-bind_check.service
    rm <dir>/yb-collect_metrics.timer
    rm <dir>/yb-collect_metrics.service
    rm <dir>/yb-master.service
    rm <dir>/yb-tserver.service
    rm <dir>/yb-controller.service
    systemctl daemon-reload
    ```

1. Delete cron job that collects metrics, cleans cores, and purges logs. Job names include, "metric collection every minute", "cleanup core files every 5 minutes", and "cleanup yb log files every 5 minutes". Note that some job files may not exist.

1. If node exporter exists, perform the following steps:

    1. Stop the node exporter service using the following command:

        ```sh
        sudo systemctl stop node_exporter
        ```

    1. Delete node exporter service under `/etc/systemd/system`, `/usr/lib/systemd/system`, or `</home/yugabyte | yb_home_dir>/.config/systemd/user` using the following command:

        ```sh
        rm <dir>/node_exporter.service
        ```

1. If otel collector service exists, perform the following steps:

    1. Stop the otel collector service using the following command:

        ```sh
        sudo systemctl stop otel-collector
        ```

    1. Delete otel collector service under `/etc/systemd/system`, `/usr/lib/systemd/system`, or `</home/yugabyte | yb_home_dir>/.config/systemd/user` using the following command:

        ```sh
        rm <dir>/otel-collector.service
        ```

1. Execute the following command:

    ```shell
    ./bin/yb-server-ctl.sh clean-instance
    ```

1. Remove node agent.

    1. Run the following node agent installer (in `node-agent/bin/`) command:

        ```sh
        ./node-agent-installer.sh  -c uninstall -u https://<yba_ip> -t <api_token> -ip <ip_of_the_node> --skip_verify_cert
        ```

    1. Remove the system unit file:

        ```sh
        rm /etc/systemd/system/yb-node-agent.service
        ```

        or

        ```sh
        rm <yugabyte_home>/.config/systemd/user/yb-node-agent.service
        ```

        depending on where it is installed.

    1. Reload systemd:

        ```sh
        systemctl daemon-reload
        ```

        or

        ```sh
        systemctl --user daemon-reload
        ```

        depending on systemd scope.

This removes all YugabyteDB code and settings from the node, removing it from the universe.

### Delete data and users

You should also erase the data from the volume mounted under the `/data` subdirectory, unless this volume is to be permanently erased by the underlying storage subsystem when the volume is deleted.

To erase this data, execute the following commands using any user with access to sudo:

```sh
sudo umount /data
```

```sh
sudo dd if=/dev/zero of=/dev/sdb bs=1M
```

The preceding command assumes the data volume is attached to the server as `/dev/sdb`.

If there is a requirement to remove the `yugabyte` user, execute the following command:

```sh
sudo userdel -r yugabyte
```

If there is a requirement to remove the `prometheus` user, execute the following command:

```sh
sudo rm -rf /opt/prometheus
```

You may now choose to reverse the system settings that you configured in [Provision nodes manually](../../prepare/server-nodes-software/software-on-prem-manual/).
