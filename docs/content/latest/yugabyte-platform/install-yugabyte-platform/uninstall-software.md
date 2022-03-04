---
title: Uninstall Yugabyte Platform software
headerTitle: Uninstall the Yugabyte Platform software
linkTitle: Uninstall software
description: Uninstall the Yugabyte Platform software.
menu:
  latest:
    identifier: uninstall-software
    parent: install-yugabyte-platform
    weight: 80
isTocNested: true
showAsideToc: true
---

To uninstall Yugabyte Platform, follow the steps for Docker or Kubernetes environments.

## Uninstall in Docker environments

1. Stop and remove the Yugabyte Platform on Replicated, as follows:

    ```sh
    /usr/local/bin/replicated apps
    ```

2. Replace *appid* with the application ID of Yugabyte Platform from the preceding command, as follows:

    ```sh
    /usr/local/bin/replicated app <appid> stop
    ```

3. Remove the Yugabyte Platform application, as follows:

    ```sh
    /usr/local/bin/replicated app <appid> rm
    ```

4. Remove all Yugabyte Platform containers, as follows:

    ```sh
    sudo docker images | grep "yuga" | awk '{print $3}' | xargs docker rmi -f
    ```

5. Delete the mapped directory, as follows:

    ```sh
    sudo rm -rf /opt/yugabyte
    ```

6. Uninstall Replicated by following instructions provided in [Removing Replicated](https://help.replicated.com/docs/native/customer-installations/installing-via-script/#removing-replicated).

## Uninstall in Kubernetes environments

1. To remove the Yugabyte Platform, execute the following helm command:

    ```sh
    helm del yw-test -n yw-test
    ```

    You should see a message similar to the following, notifying you that the Yugabyte Platform release and the namespace is deleted:

    ```output
    release "yw-test" uninstalled
    ```

2. Execute the following command to remove the namespace:

    ```sh
    kubectl delete namespace yw-test
    ```

    You should see a message similar to the following:

    ```output
    namespace "yw-test" deleted
    ```
