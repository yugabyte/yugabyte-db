---
title: Configure the Kubernetes cloud provider
headerTitle: Configure the Kubernetes cloud provider
linkTitle: Configure the cloud provider
description: Configure the Kubernetes cloud provider
menu:
  stable:
    identifier: set-up-cloud-provider-5-kubernetes
    parent: configure-yugabyte-platform
    weight: 20
isTocNested: false
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/aws" class="nav-link">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/gcp" class="nav-link">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/vmware-tanzu" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

<li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/openshift" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises" class="nav-link">
      <i class="fas fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

This document describes how to configure the Kubernetes provider for YugabyteDB universes using the Yugabyte Platform. If no cloud providers are configured in the Yugabyte Platform console yet, the main Dashboard page highlights the need to configure at least one cloud provider, as per the following illustration:

![Configure Cloud Provider](/images/ee/configure-cloud-provider.png)

## Prerequisites

### Kubernetes

If you plan to run YugabyteDB universes on Kubernetes, all you need to provide in the Yugabyte Platform console is your Kubernetes provider credentials. The Yugabyte Platform uses those credentials to automatically provision and de-provision the pods that run Yugabyte.

Before you install YugabyteDB on a Kubernetes cluster, perform the following:

- Create a `yugabyte-platform-universe-management` service account.
- Create a `kubeconfig` file of the earlier-created service account to configure access to the Kubernetes cluster.

### Service account

This is the ServiceAccount whose secret can be used to generate a `kubeconfig` file.

This account:

- Should not be deleted once it is in use by the platform.
- `namespace` in the ServiceAccount creation command can be replaced by the desired namespace in which to install YugabyteDB.

Run the following `kubectl` command to apply the YAML file:

```sh
kubectl apply -f https://raw.githubusercontent.com/yugabyte/charts/master/rbac/yugabyte-platform-universe-management-sa.yaml -n <namespace>
```

Expect the following output:

```
serviceaccount/yugabyte-platform-universe-management created
```

You need to grant access to this ServiceAccount using ClusterRoles and Roles as well as ClusterRoleBindings and RoleBindings, thus allowing it to manage the YugabyteDB universe's resources for you.
Ensure that you have replaced the `namespace` from the commands with the correct namespace of the previously created ServiceAccount.

The tasks you can perform depend on your access level.

**Global Admin** can grant broad cluster level admin access by executing the following command:

```sh
curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-global-admin.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: <namespace>"/g \
  | kubectl apply -n <namespace> -f -
```

**Global Restricted** can grant access to only the specific cluster roles to create and manage YugabyteDB universes across all the namespaces in a cluster using the following command:

```sh
curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-global.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: <namespace>"/g \
  | kubectl apply -n <namespace> -f -
```

This contains ClusterRoles and ClusterRoleBindings for the required set of permissions.

**Namespace Admin** can grant namespace level admin access by using the following command:

```sh
curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-namespaced-admin.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: <namespace>"/g \
  | kubectl apply -n <namespace> -f -
```

If you have multiple target namespaces, then you have to apply the YAML in all of them.

**Namespace Restricted** can grant access to only the specific roles required to create and manage YugabyteDB universes in a particular namespace. Contains Roles and RoleBindings for the required set of permissions.

For example, if your goal is to allow the platform software to manage YugabyteDB universes in the namespaces `yb-db-demo` and `yb-db-us-east4-a` (the target namespaces), then you need to apply in both the target namespaces.

```sh
curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-namespaced.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: <namespace>"/g \
  | kubectl apply -n <namespace> -f -
```

### `kubeconfig` file for a Kubernetes cluster

You can create a `kubeconfig` file for previously created `yugabyte-platform-universe-management` service account as follows:

1. Run the following `wget` command to get the Python script for generating the `kubeconfig` file:

    ```sh
    wget https://raw.githubusercontent.com/YugaByte/charts/master/stable/yugabyte/generate_kubeconfig.py
    ```

2. Run the following command to generate the `kubeconfig` file:

    ```sh
    python generate_kubeconfig.py -s yugabyte-platform-universe-management -n <namespace>
    ```

    The following output should appear:

    ```
    Generated the kubeconfig file: /tmp/yugabyte-platform-universe-management.conf
    ```

3. Use this generated `kubeconfig` file as the `kubeconfig` in the Yugabyte Platform Kubernetes provider configuration.

## Select the Kubernetes service

You can use the Pivotal Container Service or Managed Kubernetes Service. 

Select the tab for the service you are using, as per the following illustration:
<img title="K8s Configuration -- Tabs" alt="K8s Configuration -- Tabs" class="expandable-image" src="/images/ee/k8s-setup/k8s-provider-tabs.png" />

Use the configuration form shown in the following illustration to select the Kubernetes provider type from **Type** (Pivotal Container Service is the default).

<img title="K8s Configuration -- empty" alt="K8s Configuration -- empty" class="expandable-image" src="/images/ee/k8s-setup/k8s-configure-empty.png" />

## Configure the cloud provider

Continue configuring your Kubernetes provider as follows:

- Give a meaningful name for your configuration.
- Choose one of the folloiwng ways to specify **Kube Config** for an availability zone:
  - Specify at **provider level** in the provider form. If specified, this configuration file is used for all availability zones in all regions.
  - Specify at **zone level** in the region form. This is required for **multi-az** or **multi-region** deployments.
- Use **Service Account** to provide the name of the service account which has necessary access to manage the cluster (see [Create cluster](../../../../deploy/kubernetes/single-zone/oss/helm-chart/#create-cluster)).
- Use **Image Registry** to specify from where to pull YugabyteDB image. Accept the default setting, unless you are hosting the registry.
- Use the **Pull Secret File** field to upload the pull secret to download the image of the Enterprise YugabyteDB that is in a private repository. Your Yugabyte sales representative should have provided this secret.

The following illustration shows the completed form:

<img title="K8s Configuration -- filled" alt="K8s Configuration -- filled" class="expandable-image" src="/images/ee/k8s-setup/k8s-configure-filled.png" />

## Configure the region and zones

Continue configuring your Kubernetes provider by clicking **Add Region** and completing the **Add new region** dialog, as follows:

- Use the `Region` field to select the region.
- Use the **Zone** field to select a zone label that should match with your failure domain zone label `failure-domain.beta.kubernetes.io/zone`.
- Optionally, use the **Storage Class** field to enter a comma-delimited value. If you do not specify this value, it would default to standard. You need to ensure that this storage class exists in your Kubernetes cluster.
- Use the **Namespace** field to specify the namespace. If provided SA has the `Cluster Admin` permissions, you are not required to complete this field. The SA used in the provided `kubeconfig` file should have access to this namespace.
- Use **Kube Config** to upload the configuration file. If this file is available at provider level, you are not required to supply it.

<img title="K8s Configuration -- zone config" alt="K8s Configuration -- zone config" class="expandable-image" src="/images/ee/k8s-setup/k8s-az-kubeconfig.png" />

- Complete the **Overrides** field using one of the provided options. If you do not specify anything, Yugabyte Platform would use defaults specified inside the Helm chart. The following overrides are available:

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
  
  - Overrides to add annotations at StatefulSet-level:

    ```yml
    networkAnnotation:
      annotation1: 'foo'
      annotation2: 'bar'
    ```

  - Overrides to add custom resource allocation for YB master and TServer pods and it overrides the instance types selected in the Yugabyte universe creation flow:
  
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

  - Overrides to enable Istio compatibility (required when Istio is used with Kubernetes):

    ```yml
    istioCompatibility: enabled: true
    ```

Continue configuring your Kubernetes provider by clicking **Add Zone** and notice that there are might be multiple zones, as per the following illustration:

<img title="K8s Configuration -- region" alt="K8s Configuration -- region" class="expandable-image" src="/images/ee/k8s-setup/k8s-add-region-flow.png" />

Finally, click **Add Region**, and then click **Save** to save the configuration. If successful, you will be redirected to the table view of all configurations.

## Next step

You are now ready to create YugabyteDB universes, as described in the next section.
