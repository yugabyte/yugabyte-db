---
title: Create a multi-zone universe using YugabyteDB Anywhere and Kubernetes
headerTitle: Create a multi-zone universe
linkTitle: Multi-zone universe
description: Use YugabyteDB Anywhere and Kubernetes to create a YugabyteDB universe that spans multiple availability zones.
menu:
  preview_yugabyte-platform:
    identifier: create-multi-zone-universe-kubernetes
    parent: create-deployments
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../create-universe-multi-zone/" class="nav-link">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
      Generic</a>
  </li>

  <li>
    <a href="../create-universe-multi-zone-kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

YugabyteDB Anywhere allows you to create a universe in one geographic region across multiple availability zones using Kubernetes as a cloud provider.

## Prerequisites

Before you start creating a universe, ensure that you performed steps described in [Create Kubernetes provider configuration](../../configure-yugabyte-platform/kubernetes/).

Note that the provider example used in this document has a cluster-level admin access.

## Create a universe

To start, navigate to **Dashboard** or **Universes**, click **Create Universe** and complete the first two fields of the **Cloud Configuration** section:

- In the **Name** field, enter the name for the YugabyteDB universe using lowercase characters (for example, yb-k8s).

- Use the **Provider** field to select the appropriate Kubernetes cloud (for example, multilane-k8s-portal-yb). Notice that additional fields appear.

Complete the rest of the **Cloud Configuration** section as follows:

- Use the **Regions** field to select the region. This enables the **Availability Zones** option that allows you to see zones belonging to that region.

- Provide the value in the **Pods** field. This value should be equal to or greater than the replication factor. The default value is 3. When this value is supplied, the pods (also known as nodes) are automatically placed across all the availability zones to guarantee the maximum availability.

- In the **Replication Factor** field, define the replication factor, as per the following illustration:

  ![Kubernetes Cloud Configuration](/images/yb-platform/kubernetes-config55.png)

### Instance Configuration

Complete the **Instance Configuration** section {{<badge/ea>}} for **TServer** and **Master** as follows:

- **Number of Cores** - specify the total number of processing cores or CPUs assigned to the TServer and Master.
- **Memory(GiB)** - specify the memory allocation of the TServer and Master.
- **Volume Info** - specify the number of volumes multiplied by size for the TServer and Master. The default is 1 x 100GB.

  ![Kubernetes Overrides](/images/yb-platform/instance-config-k8s.png)

### Security Configurations

Complete the **Security Configurations** section as follows:

- **Enable YSQL** - specify whether or not to enable the YSQL API endpoint for running PostgreSQL-compatible workloads. This setting is enabled by default.
- **Enable YSQL Auth** - specify whether or not to enable the YSQL password authentication.
- **Enable YCQL** - specify whether or not to enable the YCQL API endpoint for running Cassandra-compatible workloads. This setting is enabled by default.
- **Enable YCQL Auth** - specify whether or not to enable the YCQL password authentication.
- **Enable Node-to-Node TLS** - specify whether or not to enable encryption in transit for communication between the database servers. This setting is enabled by default.
- **Enable Client-to-Node TLS** - specify whether or not to enable encryption in transit for communication between clients and the database servers. This setting is enabled by default.
- **Root Certificate** - select an existing security certificate or create a new one.
- **Enable Encryption at Rest** - specify whether or not to enable encryption for data stored on the tablet servers. This setting is disabled by default.

### Advanced Configuration

Complete the **Advanced** section as follows:

- In the **DB Version** field, specify the YugabyteDB version. The default is either the same as the YugabyteDB Anywhere version or the latest YugabyteDB version available for YugabyteDB Anywhere. If the version you want to add is not listed, you can add it to YugabyteDB Anywhere. Refer to [Manage YugabyteDB releases](../../manage-deployments/ybdb-releases/).
- Use the **Enable IPV6** field to specify whether or not you want to use IPV6 networking for connections between database servers. This setting is disabled by default.
- Use the **Enable Public Network Access** field to specify whether or not to assign a load balancer or nodeport for connecting to the database endpoints over the internet. This setting is disabled by default.

### Configure G-Flags

Optionally, complete the **G-Flags** section as follows:

- Click **Add Flags > Add to Master** to specify YB-Master servers parameters, one parameter per field.

- Click **Add Flags > Add to T-Server** to specify YB-TServer servers parameters, one parameter per field.

  For details, see the following:

  - [Edit configuration flags](../../manage-deployments/edit-config-flags)

  - [YB-Master configuration flags](../../../reference/configuration/yb-master/#configuration-flags)

  - [YB-TServer configuration flags](../../../reference/configuration/yb-tserver/#configuration-flags)

### Configure Helm overrides

Optionally, use the **Helm Overrides** section, as follows:

- Click **Add Kubernetes Overrides** to open the **Kubernetes Overrides** dialog shown in the following illustration:

  ![Kubernetes Overrides](/images/yb-platform/kubernetes-config66.png)

- Using the YAML format (which is sensitive to spacing and indentation), specify the universe-level overrides for YB-Master and YB-TServer, as per the following example:

  ```yaml
  master:
    podLabels:
      service-type: 'database'
  ```

- Add availability zone overrides, which only apply to pods that are deployed in that specific availability zone. For example, to define overrides for the availability zone us-west-2a, you would click **Add Availability Zone** and use the text area to insert YAML in the following form:

  ```yaml
  us-west-2a:
    master:
      podLabels:
         service-type: 'database'
  ```

  If you specify conflicting overrides, YugabyteDB Anywhere would use the following order of precedence: universe availability zone-level overrides, universe-level overrides, provider overrides.

- If you want to enable [GKE service account-based IAM](../../back-up-restore-universes/configure-backup-storage/#gke-service-account-based-iam-gcp-iam) for backup and restore using GCS at the universe level, add the following overrides:

    ```yaml
    tserver:
      serviceAccount: <KSA_NAME>
    nodeSelector:
      iam.gke.io/gke-metadata-server-enabled: "true"
    ```

    If you don't provide namespace names for each zone/region during [provider creation](../../configure-yugabyte-platform/kubernetes/), add the names using the following steps:

    1. Add the Kubernetes service account to the namespaces where the pods are created.
    1. Follow the steps in [Upgrade universes for GKE service account-based IAM](../../manage-deployments/edit-helm-overrides/#upgrade-universes-for-gke-service-account-based-iam) to add the annotated Kubernetes service account to pods.

    To enable the GKE service account service at the provider level, refer to [Overrides](../../configure-yugabyte-platform/kubernetes/#overrides).

- Select **Force Apply** if you want to override any previous overrides.

- Click **Validate & Save**.

  If there are any errors in your overrides definitions, a detailed error message is displayed. You can correct the errors and try to save again. To save your Kubernetes overrides regardless of any validation errors, select **Force Apply**.

The final step is to click **Create** and wait for the YugabyteDB cluster to appear.

The following illustration shows the universe in its pending state:

![Pending universe](/images/yb-platform/kubernetes-config10.png)

## Examine the universe and connect to nodes

The universe view consists of several tabs that provide different information about this universe.

The following illustration shows the **Overview** tab of a newly-created universe:

![Universe Overview](/images/yb-platform/kubernetes-config11.png)

If you have defined Helm overrides for your universe, you can modify them at any time through **Overview** by clicking **Actions > Edit Kubernetes Overrides**.

The following illustration shows the **Nodes** tab that allows you to see a list of nodes with their addresses:

![Universe Nodes](/images/yb-platform/kubernetes-config12.png)

You can create a connection to a node as follows:

- Click **Connect** to access the information about the universe's endpoints to which to connect.

- On a specific node, click **Actions > Connect** to access the `kubectl` commands that you need to copy and use to connect to the node.

## Connect to the universe

For information on how to connect to the universe from the Kubernetes cluster, as well as remotely, see [Connect YugabyteDB clusters](../../../deploy/kubernetes/clients/#connect-tls-secured-yugabytedb-cluster-deployed-by-helm-charts).

### Create common YB-TServer service for zones

By default, each zone has its own YB-TServer service, and you can use this service to connect to the universe. Optionally, you can create an additional highly available common service across all zones as follows.

Note that this requires all the zone deployments to be in the same namespace.

1. Set the following Kubernetes overrides to add the universe-name label on the YB-TServer pods. You can do this when you [create the universe](#configure-helm-overrides) or by [modifying the Kubernetes overrides](../../manage-deployments/edit-helm-overrides/) of an existing universe.

   ```yaml
   tserver:
     podLabels:
       universe-name: yb-k8s
   ```

1. Save the following to a file named `yb-tserver-common-service.yaml`. You can customize the service type, annotations, and the label selector as required.

   ```yaml
   # yb-tserver-common-service.yaml
   apiVersion: v1
   kind: Service
   metadata:
     name: yb-k8s-common-tserver
     labels:
       app.kubernetes.io/name: yb-tserver
     # annotations:
     #   networking.gke.io/load-balancer-type: "Internal"
   spec:
     type: ClusterIP
     selector:
       app.kubernetes.io/name: yb-tserver
       # This value should match with the value from step 1.
       universe-name: yb-k8s
     ports:
     # Modify the ports if using non-standard ports.
     - name: tcp-yql-port
       port: 9042
     - name: tcp-ysql-port
       port: 5433
   ```

1. Run the following command to create the service in the universe's namespace (`yb-prod-yb-k8s` in this example).

   ```sh
   $ kubectl apply -f yb-tserver-common-service.yaml -n yb-prod-yb-k8s
   ```

After the service YAML is applied, in this example you would access the universe at `yb-k8s-common-tserver.yb-prod-yb-k8s.svc.cluster.local`.
