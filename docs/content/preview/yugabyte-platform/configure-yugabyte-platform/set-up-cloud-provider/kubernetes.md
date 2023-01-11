---
..title: Configure the Kubernetes cloud provider
headerTitle: Configure the Kubernetes cloud provider
linkTitle: Configure cloud providers
description: Configure the Kubernetes cloud provider
aliases:
  - /preview/deploy/enterprise-edition/configure-cloud-providers/kubernetes
menu:
  preview_yugabyte-platform:
    identifier: set-up-cloud-provider-5-kubernetes
    parent: configure-yugabyte-platform
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws/" class="nav-link">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure/" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../vmware-tanzu/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

<li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="../on-premises/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

This document describes how to configure the Kubernetes provider for YugabyteDB universes using YugabyteDB Anywhere. If no cloud providers are configured in YugabyteDB Anywhere yet, the main **Dashboard** page prompts you to configure at least one cloud provider.

## Prerequisites

To run YugabyteDB universes on Kubernetes, all you need to provide in YugabyteDB Anywhere is your Kubernetes provider credentials. YugabyteDB Anywhere uses those credentials to automatically provision and de-provision the pods that run YugabyteDB.

Before you install YugabyteDB on a Kubernetes cluster, perform the following:

- Create a `yugabyte-platform-universe-management` service account.
- Create a `kubeconfig` file of the earlier-created service account to configure access to the Kubernetes cluster.

### Service account

The secret of a service account can be used to generate a `kubeconfig` file. This account should not be deleted once it is in use by YugabyteDB Anywhere.

Set the `YBA_NAMESPACE` environment variable to the namespace where your YugabyteDB Anywhere is installed, as follows:

```sh
export YBA_NAMESPACE="yb-platform"
```

Note that the `YBA_NAMESPACE` variable is used in the commands throughout this document.

Run the following `kubectl` command to apply the YAML file:

```sh
export YBA_NAMESPACE="yb-platform"

kubectl apply -f https://raw.githubusercontent.com/yugabyte/charts/master/rbac/yugabyte-platform-universe-management-sa.yaml -n ${YBA_NAMESPACE}
```

Expect the following output:

```output
serviceaccount/yugabyte-platform-universe-management created
```

The next step is to grant access to this service account using ClusterRoles and Roles, as well as ClusterRoleBindings and RoleBindings, thus allowing it to manage the YugabyteDB universe's resources for you.

The namespace in the following commands needs to be replaced with the correct namespace of the previously created service account.

The tasks you can perform depend on your access level.

**Global Admin** can grant broad cluster level admin access by executing the following command:

```sh
export YBA_NAMESPACE="yb-platform"

curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-global-admin.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: ${YBA_NAMESPACE}"/g \
  | kubectl apply -n ${YBA_NAMESPACE} -f -
```

**Global Restricted** can grant access to only the specific cluster roles to create and manage YugabyteDB universes across all the namespaces in a cluster using the following command:

```sh
export YBA_NAMESPACE="yb-platform"

curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-global.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: ${YBA_NAMESPACE}"/g \
  | kubectl apply -n ${YBA_NAMESPACE} -f -
```

This contains ClusterRoles and ClusterRoleBindings for the required set of permissions.

**Namespace Admin** can grant namespace-level admin access by using the following command:

```sh
export YBA_NAMESPACE="yb-platform"

curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-namespaced-admin.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: ${YBA_NAMESPACE}"/g \
  | kubectl apply -n ${YBA_NAMESPACE} -f -
```

If you have multiple target namespaces, then you have to apply the YAML in all of them.

**Namespace Restricted** can grant access to only the specific roles required to create and manage YugabyteDB universes in a particular namespace. Contains Roles and RoleBindings for the required set of permissions.

For example, if your goal is to allow YugabyteDB Anywhere to manage YugabyteDB universes in the namespaces `yb-db-demo` and `yb-db-us-east4-a` (the target namespaces), then you need to apply in both the target namespaces, as follows:

```sh
export YBA_NAMESPACE="yb-platform"

curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-namespaced.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: ${YBA_NAMESPACE}"/g \
  | kubectl apply -n ${YBA_NAMESPACE} -f -
```

### kubeconfig file

You can create a `kubeconfig` file for the previously created `yugabyte-platform-universe-management` service account as follows:

1. Run the following `wget` command to get the Python script for generating the `kubeconfig` file:

    ```sh
    wget https://raw.githubusercontent.com/YugaByte/charts/master/stable/yugabyte/generate_kubeconfig.py
    ```

2. Run the following command to generate the `kubeconfig` file:

    ```sh
    export YBA_NAMESPACE="yb-platform"

    python generate_kubeconfig.py -s yugabyte-platform-universe-management -n ${YBA_NAMESPACE}
    ```

    Expect the following output:

    ```output
    Generated the kubeconfig file: /tmp/yugabyte-platform-universe-management.conf
    ```

3. Use this generated `kubeconfig` file as the `kubeconfig` in the YugabyteDB Anywhere Kubernetes provider configuration.

## Select the Kubernetes service

In the YugabyteDB Anywhere UI, navigate to **Configs > Cloud Provider Configuration > Managed Kubernetes Service** and select one of the Kubernetes service providers using the **Type** field, as per the following illustration:

![Kubernetes config](/images/ee/k8s-setup/k8s-configure-empty.png)

## Configure the cloud provider

Continue configuring your Kubernetes provider as follows:

1. Specify a meaningful name for your configuration.
2. Choose one of the following ways to specify **Kube Config** for an availability zone:
   - Specify at **provider level** in the provider form. If specified, this configuration file is used for all availability zones in all regions.
   - Specify at **zone level** in the region form. This is required for **multi-az** or **multi-region** deployments. If the zone is in a different Kubernetes cluster than YugabyteDB Anywhere, a zone-specific `kubeconfig` file needs to be passed.
3. In the **Service Account** field, provide the name of the [service account](#service-account) which has necessary access to manage the cluster (see [Create cluster](../../../../deploy/kubernetes/single-zone/oss/helm-chart/#create-cluster)).
4. In the **Image Registry** field, specify from where to pull the YugabyteDB image. Accept the default setting, unless you are hosting the registry, in which case refer to steps described in [Pull and push YugabyteDB Docker images to private container registry](../../../install-yugabyte-platform/prerequisites#pull-and-push-yugabytedb-docker-images-to-private-container-registry).
5. Use **Pull Secret File** to upload the pull secret to download the image of the Enterprise YugabyteDB that is in a private repository. Your Yugabyte sales representative should have provided this secret.

## Configure region and zones

Continue configuring your Kubernetes provider by clicking **Add region** and completing the **Add new region** dialog shown in the following illustration: 

![Add new region](/images/ee/k8s-setup/k8s-az-kubeconfig.png)

1. Use the **Region** field to select the region.

1. Use the **Zone** field to select a zone label that should match the value of failure domain zone label on the nodes. `topology.kubernetes.io/zone` would place the pods in that zone.

1. Optionally, use the **Storage Class** field to enter a comma-delimited value. If you do not specify this value, it would default to standard. You need to ensure that this storage class exists in your Kubernetes cluster and the following guidelines are taken into consideration:

   - Volume binding mode should be set to `WaitForFirstConsumer`, as described in [Configure storage class volume binding](../../../troubleshoot/universe-issues/#configure-storage-class-volume-binding).

   - An SSD-based storage class and an extent-based file system (XFS) should be used, as per recommendations provided in [Deployment checklist - Disks](../../../../deploy/checklist/#disks).

     The following is a sample storage class YAML file for Google Kubernetes Engine (GKE). You are expected to modify it to suit your Kubernetes cluster:

      ```yaml
      kind: StorageClass
      metadata:
      	name: yb-storage
      provisioner: kubernetes.io/gce-pd
      volumeBindingMode: WaitForFirstConsumer
      allowVolumeExpansion: true
      reclaimPolicy: Delete
      parameters:
      	type: pd-ssd
      	fstype: xfs
      ```

1. Use the **Namespace** field to specify the namespace. If provided service account has the `Cluster Admin` permissions, you are not required to complete this field. The service account used in the provided `kubeconfig` file should have access to this namespace.

1. Use **Kube Config** to upload the configuration file. If this file is available at the provider level, you are not required to supply it. 

1. Complete the **Overrides** field using one of the provided [options](#overrides). If you do not specify anything, YugabyteDB Anywhere uses defaults specified inside the Helm chart. For additional information, see [Open source Kubernetes](../../../../deploy/kubernetes/single-zone/oss/helm-chart/).

1. Click **Add Zone** and complete the corresponding portion of the dialog. Notice that there are might be multiple zones.

1. Finally, click **Add Region**, and then click **Save** to save the configuration. If successful, you will be redirected to the table view of all configurations.


### Overrides

The following overrides are available:

- Overrides to add service-level annotations:

  ```yml
  serviceEndpoints:
    - name: "yb-master-service"
      type: "LoadBalancer"
      annotations:
        service.beta.kubernetes.io/aws-load-balancer-internal: "0.0.0.0/0"
      app: "yb-master"
      ports:
        ui: "7000"
  
    - name: "yb-tserver-service"
      type: "LoadBalancer"
      annotations:
        service.beta.kubernetes.io/aws-load-balancer-internal: "0.0.0.0/0"
      app: "yb-tserver"
      ports:
        ycql-port: "9042"
        yedis-port: "6379"
        ysql-port: "5433"
  ```

- Overrides to disable LoadBalancer:

  ```yml
  enableLoadBalancer: False
  ```

- Overrides to change the cluster domain name:

  ```yml
  domainName: my.cluster
  ```

  YugabyteDB servers and other components communicate with each other using the Kubernetes Fully Qualified Domain Names (FQDN). The default domain is `cluster.local`.

- Overrides to add annotations at StatefulSet-level:

  ```yml
  networkAnnotation:
    annotation1: 'foo'
    annotation2: 'bar'
  ```

- Overrides to add custom resource allocation for YB-Master and YB-TServer pods. This overrides instance types selected in the YugabyteDB Anywhere universe creation flow:

  ```yml
  resource:
    master:
      requests:
        cpu: 2
        memory: 2Gi
      limits:
        cpu: 2
        memory: 2Gi
    tserver:
      requests:
        cpu: 2
        memory: 4Gi
      limits:
        cpu: 2
        memory: 4Gi
  ```

- Overrides to enable Istio compatibility:

  ```yml
  istioCompatibility: enabled: true
  ```

  This is required when Istio is used with Kubernetes.

- Overrides to publish Node-IP as the server broadcast address:

  By default, YB-Master and YB-TServer pod fully-qualified domain names (FQDN) are used within the cluster as the server broadcast address. To publish the IPs of the nodes on which YB-TServer pods are deployed, add the following YAML to each zone override configuration: 

  ```yml
  tserver:
    extraEnv:
    - name: NODE_IP
      valueFrom:
        fieldRef:
          fieldPath: status.hostIP
    serverBroadcastAddress: "$(NODE_IP)"
    affinity:
      podAntiAffinity:
        requiredDuringSchedulingIgnoredDuringExecution:
        - labelSelector:
            matchExpressions:
            - key: app
              operator: In
              values:
              - "yb-tserver"
          topologyKey: kubernetes.io/hostname
  
  # Required to esure that the Kubernetes FQDNs are used for
  # internal communication between the nodes and node-to-node
  # TLS certificates are validated correctly
  
  gflags:
    master:
      use_private_ip: cloud
    tserver:
      use_private_ip: cloud
  
  serviceEndpoints:
    - name: "yb-master-ui"
      type: LoadBalancer
      app: "yb-master"
      ports:
        http-ui: "7000"
  
    - name: "yb-tserver-service"
      type: NodePort
      externalTrafficPolicy: "Local"
      app: "yb-tserver"
      ports:
        tcp-yql-port: "9042"
        tcp-yedis-port: "6379"
        tcp-ysql-port: "5433"
  ```

- Overrides to run YugabyteDB as a non-root user:

  ```yml
  podSecurityContext:
    enabled: true
    ## Set to false to stop the non-root user validation
    runAsNonRoot: true
    fsGroup: 10001
    runAsUser: 10001
    runAsGroup: 10001
  ```

  Note that you cannot change users during the Helm upgrades.

- Overrides to add `tolerations` in YB-Master and YB-TServer pods:

  ```yml
  ## Consider the node has the following taint:
  ## kubectl taint nodes node1 dedicated=experimental:NoSchedule-
  
  master:
    tolerations:
    - key: dedicated
      operator: Equal
      value: experimental
      effect: NoSchedule
  
  tserver:
    tolerations: []
  ```

  Tolerations work in combination with taints: `Taints` are applied on nodes and `Tolerations` are applied to pods. Taints and tolerations ensure that pods do not schedule onto inappropriate nodes. You need to set `nodeSelector` to schedule YugabyteDB pods onto specific nodes, and then use taints and tolerations to prevent other pods from getting scheduled on the dedicated nodes, if required. For more information, see [toleration](../../../install-yugabyte-platform/install-software/kubernetes/#toleration) and [Toleration API](https://kubernetes.io/docs/reference/generated/kubernetes-api/v1.22/#toleration-v1-core).

- Overrides to use `nodeSelector` to schedule YB-Master and YB-TServer pods on dedicated nodes: 

  ```yml
  ## To schedule a pod on a node that has a topology.kubernetes.io/zone=asia-south2-a label
  
  nodeSelector:
    topology.kubernetes.io/zone: asia-south2-a
  ```

  For more information, see [nodeSelector](../../../install-yugabyte-platform/install-software/kubernetes/#nodeselector) and [Kubernetes: Node Selector](https://kubernetes.io/docs/concepts/scheduling-eviction/assign-pod-node/#nodeselector).

- Overrides to add `affinity` in YB-Master and YB-TServer pods:

  ```yml
  ## To prevent scheduling of multiple master pods on single kubernetes node
  
  master:
    affinity:
      podAntiAffinity:
        requiredDuringSchedulingIgnoredDuringExecution:
        - labelSelector:
            matchExpressions:
            - key: app
              operator: In
              values:
              - "yb-master"
          topologyKey: kubernetes.io/hostname
  
  tserver:
    affinity: {}
  ```

  `affinity` allows the Kubernetes scheduler to place a pod on a set of nodes or a pod relative to the placement of other pods. You can use `nodeAffinity` rules to control pod placements on a set of nodes. In contrast, `podAffinity` or `podAntiAffinity` rules provide the ability to control pod placements relative to other pods. For more information, see [Affinity API](https://kubernetes.io/docs/reference/generated/kubernetes-api/v1.22/#affinity-v1-core).

- Overrides to add `annotations` to YB-Master and YB-TServer pods:

  ```yml
  master:
    podAnnotations:
      application: "yugabytedb"
  
  tserver:
    podAnnotations: {}
  ```

  The Kubernetes `annotations` can attach arbitrary metadata to objects. For more information, see [Annotations](https://kubernetes.io/docs/concepts/overview/working-with-objects/annotations/).

- Overrides to add `labels` to YB-Master and YB-TServer pods:

  ```yml
  master:
    podLabels:
      environment: production
      app: yugabytedb
      prometheus.io/scrape: true
  
  tserver:
    podLabels: {}
  ```

  The Kubernetes `labels` are key-value pairs attached to objects. The `labels` are used to specify identifying attributes of objects that are meaningful and relevant to you. For more information, see [Labels](https://kubernetes.io/docs/concepts/overview/working-with-objects/labels/).

- Preflight check overrides, such as DNS address resolution, disk IO, available port, ulimit:

  ```yml
  ## Default values
  preflight:
    ## Set to true to skip disk IO check, DNS address resolution, port bind checks
    skipAll: false
  
    ## Set to true to skip port bind checks
    skipBind: false
  
    ## Set to true to skip ulimit verification
    ## SkipAll has higher priority
    skipUlimit: false
  ```

  For more information, see [Helm chart: Prerequisites](../../../../deploy/kubernetes/single-zone/oss/helm-chart/#prerequisites).

- Overrides to use a secret for LDAP authentication. Refer to [Create secrets for Kubernetes](../../../../secure/authentication/ldap-authentication-ysql/#create-secrets-for-kubernetes).
