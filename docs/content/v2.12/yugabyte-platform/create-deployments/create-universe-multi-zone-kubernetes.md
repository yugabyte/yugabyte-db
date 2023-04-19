---
title: Create a multi-zone universe using Yugabyte Platform and Kubernetes
headerTitle: Create a multi-zone universe with Kubernetes
linkTitle: Multi-zone universe
description: Use Yugabyte Platform and Kubernetes to create a YugabyteDB universe that spans multiple availability zones.
menu:
  v2.12_yugabyte-platform:
    identifier: create-multi-zone-universe-kubernetes
    parent: create-deployments
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

<li>
    <a href="../create-universe-multi-zone" class="nav-link">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
Generic</a>
  </li>

  <li>
    <a href="../create-universe-multi-zone-kubernetes" class="nav-link active">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

This section describes how to create a YugabyteDB universe in one geographic region across multiple availability zones using Kubernetes as a cloud provider.

## Prerequisites

Before you start creating a universe, ensure that you performed steps described in [Configure the Kubernetes cloud provider](/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes/). The following illustration shows the **Managed Kubernetes Service configs** page that you should be able to see if you use Yugabyte Platform to navigate to **Configs > Cloud Provider Configuration > Infrastructure > Managed Kubernetes Service**:

![img](/images/yb-platform/kubernetes-config1.png)

Note that the cloud provider example used in this document has a cluster-level admin access.

## Create a universe

If no universes have been created yet, the Yugabyte Platform Dashboard looks similar to the following:

![img](/images/yb-platform/kubernetes-config2.png)

To start, click **Create Universe** and complete the first two fields of the **Cloud Configuration** section shown in the following illustration:

![img](/images/yb-platform/kubernetes-config3.png)

- In the **Name** field, enter the name for the YugabyteDB universe (or example, yb-k8s).

- Use the **Provider** field to select the appropriate Kubernetes cloud (or example, K8s Provider). Notice that additional fields are now displayed, as shown in the following illustration:

   ![img](/images/yb-platform/kubernetes-config4.png)

Complete the rest of the **Cloud Configuration** section as follows:

- Use the **Region** field to select the region. This enables the **Availability Zones** option that allows you to see zones belonging to that region.

- Provide the value in the **Pods** field. This value should be equal to or greater than the replication factor. The default value is 3. When this value is supplied, the pods are automatically placed across all the availability zones to guarantee the maximum availability.

- In the **Replication Factor** field, define the replication factor. The default is 3.

  ![img](/images/yb-platform/kubernetes-config5.png)

Complete the **Instance Configuration** section as follows:

- Use the **Instance Type** field to select the CPU and memory combination, as per needs to allocate the YB-TServer pods. The default is Small (4 Cores & 7.5GB RAM). You can override this setting when you configure the Kubernetes cloud provider (see [Configuring the region and zones](/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes/#configure-the-region-and-zones)).

- In the **Volume Info** field, specify the number of volumes multiplied by size. The default is 1*100GB.

- Use the **Enable YSQL** field to enable the YSQL API endpoint to run Postgres-compatible workloads. This setting is enabled by default.

- Use the **Enable YEDIS** field to enable the YEDIS API endpoint to run REDIS-compatible workloads. This setting is disabled by default.

- Use the **Enable Node-to-Node TLS** field to enable encryption-in-transit for communication between the database servers. This setting is enabled by default in Yugabyte Platform version 2.7.1.1 and later.

- Use the **Enable Client-to-Node TLS** field to enable encryption-in-transit for communication between clients and the database servers. This setting is enabled by default in Yugabyte Platform version 2.7.1.1 and later.

- Use the **Enable Encryption at Rest** field to enable encryption for data stored on the tablet servers. This setting is disabled by default.

  ![img](/images/yb-platform/kubernetes-config6.png)

Complete the **Advanced** section as follows:

- In the **DB Version** field, specify the YugabyteDB version. The default is either the same as the Yugabyte Platform version or the latest YugabyteDB version available for Yugabyte Platform.

- Use the **Enable IPV6** to enable the use of IPV6 networking for connections between the database servers. This setting is enabled by default.

- Use the **Enable Public Network Access** field to assign a load balancer or nodePort for connecting to the database endpoints over the internet. This setting is enabled by default.

  ![img](/images/yb-platform/kubernetes-config7.png)

Complete the **G-Flags** section as follows:

- Use the **Master** fields to specify YugabyteDB Masters parameters, one parameter per field (see [YB Master Configuration Flags](/preview/reference/configuration/yb-master/#configuration-flags) for details).

- Use the **T-Server** fields to specify the YugabyteDB T-Servers parameters, one parameter per field (see [YB T-Server Configuration Flags](/preview/reference/configuration/yb-tserver/#configuration-flags) for details).

  ![img](/images/yb-platform/kubernetes-config8.png)

Accept default values for all of the remaining fields.

The following illustration shows the completed **Cloud Configuration** page:

![img](/images/yb-platform/kubernetes-config9.png)

The final step is to click **Create** and wait for the YugabyteDB cluster to appear.

The following illustration shows the universe in its pending state:

![img](/images/yb-platform/kubernetes-config10.png)

## Examine the universe and connect to pods

The universe view consists of several tabs that provide different information about this universe.

The following illustration shows the **Overview** tab of a newly-created universe:

![img](/images/yb-platform/kubernetes-config11.png)

The following illustration shows the **Pods** tab that allows you to see a list of pods with their addresses:

![img](/images/yb-platform/kubernetes-config12.png)

You can create a connection to a pod as follows:

- Click **Connect** to access the universe's endpoints to connect, as shown in the following illustration:

    ![img](/images/yb-platform/kubernetes-config13.png)

- Click **Actions** on a specific pod, as per the following illustration:

    ![img](/images/yb-platform/kubernetes-config14.png)

- Click **Actions > Connect** to access the `kubectl` commands that allow you to connect to the pods, as shown in the following illustration:

    ![img](/images/yb-platform/kubernetes-config15.png)

## Connect to the universe

For information on how to connect to the universe from the Kubernetes cluster as well as remotely, see [Connecting YugabyteDB Clusters](/preview/deploy/kubernetes/clients/#connecting-tls-secured-yugabytedb-cluster-deployed-by-helm-charts).
