---
title: Configure the Kubernetes cloud provider
headerTitle: Configure the Kubernetes cloud provider
linkTitle: Configure the cloud provider
description: Configure the Kubernetes cloud provider
menu:
  v2.6:
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

This page details how to configure the Kubernetes provider for YugabyteDB universes using the Yugabyte Platform. If no cloud providers are configured in the Yugabyte Platform console yet, the main Dashboard page highlights the need to configure at least one cloud provider.

![Configure Cloud Provider](/images/ee/configure-cloud-provider.png)

## Prerequisites

### Kubernetes

If you plan to run YugabyteDB universes on Kubernetes, all you need to provide in the Yugabyte Platform console is your Kubernetes provider credentials. The Yugabyte Platform uses those credentials to automatically provision and de-provision the pods that run Yugabyte.

Before you install YugabyteDB on a Kubernetes cluster, perform the following:

- Create a `yugabyte-platform-universe-management` service account.
- Create a `kubeconfig` file of the earlier-created service account to configure access to the Kubernetes cluster.

### Service account creation

This is the ServiceAccount whose secret can be used to generate a kubeconfig.

**Notes**

- It should not be deleted once it is in use by the platform.
- `namespace` in the ServiceAccount creation command can be replaced by the desired namespace in which to install YugabyteDB.

Run the following `kubectl` command to apply the YAML file:

```sh
kubectl apply -f https://raw.githubusercontent.com/yugabyte/charts/master/rbac/yugabyte-platform-universe-management-sa.yaml -n <namespace>
```

The following output should appear:

```
serviceaccount/yugabyte-platform-universe-management created
```

The next step is to grant access to this ServiceAccount using ClusterRoles/Roles and ClusterRoleBindings/RoleBindings, thus allowing it to manage the YugabyteDB universe's resources for you.
Follow any one of the following steps depending on your requirements.

**Notes**
- Make sure you replace the `namespace` from the commands with the correct namespace of the previously created ServiceAccount.

**Global Admin**

Grants broad cluster level admin access.

```sh
curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-global-admin.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: <namespace>"/g \
  | kubectl apply -n <namespace> -f -
```

**Global Restricted**

Grants access to only the specific cluster roles to create and manage YugabyteDB universes across all the namespaces in a cluster. Contains ClusterRoles and ClusterRoleBindings for the required set of permissions.

```sh
curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-global.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: <namespace>"/g \
  | kubectl apply -n <namespace> -f -
```

**Namespace Admin**

Grants namespace level admin access.

If you have multiple target namespaces, then you have to apply the YAML in all of them.

```sh
curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-namespaced-admin.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: <namespace>"/g \
  | kubectl apply -n <namespace> -f -
```

**Namespace Restricted**

Grants access to only the specific roles required to create and manage YugabyteDB universes in a particular namespace only. Contains Roles and RoleBindings for the required set of permissions.

*Example:* If your goal is to allow the platform software to manage YugabyteDB universes in the namespaces `yb-db-demo` and `yb-db-us-east4-a` (the target namespaces), then you need to apply in both the target namespaces.

```sh
curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-namespaced.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: <namespace>"/g \
  | kubectl apply -n <namespace> -f -
```

### Create a `kubeconfig` File for a Kubernetes Cluster

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

## Configure Kubernetes credentials

## Pick appropriate k8s tab

For Kubernetes, you have two options: using Pivotal Container Service, or Managed Kubernetes Service. Click the tab for the service you're using.
<img title="K8s Configuration -- Tabs" alt="K8s Configuration -- Tabs" class="expandable-image" src="/images/ee/k8s-setup/k8s-provider-tabs.png" />

Once you go to the appropriate tab, you should see a configuration form like this:

<img title="K8s Configuration -- empty" alt="K8s Configuration -- empty" class="expandable-image" src="/images/ee/k8s-setup/k8s-configure-empty.png" />

Select the Kubernetes provider type from **Type**. In the case of Pivotal Container Service, this would be default to that option.

## Configure the cloud provider

Take note of the following for configuring your Kubernetes provider:

- Give a meaningful name for your configuration.

- **Service Account** provide the name of the service account which has necessary access to manage the cluster, refer to [Create cluster](../../../../deploy/kubernetes/single-zone/oss/helm-chart/#create-cluster).

- **Kube Config** there are two ways to specify the kube config for an availability zone.
  - Specify at **provider level** in the provider form as shown above. If specified, this configuration file will be used for all availability zones in all regions.
  - Specify at **zone level** inside of the region form as described below, this is especially needed for **multi-az** or **multi-region** deployments.

- **Image Registry** specifies where to pull YugabyteDB image from leave this to default, unless you are hosting the registry on your end.

- **Pull Secret**, the Enterprise YugabyteDB image is in a private repo and you need to upload the pull secret to download the image, your sales representative should have provided this secret.

A filled in form looks something like this:

<img title="K8s Configuration -- filled" alt="K8s Configuration -- filled" class="expandable-image" src="/images/ee/k8s-setup/k8s-configure-filled.png" />

## Configure the region and zones

Click **Add Region** to open the modal.

- Specify a Region and the dialog will expand to show the zone form.

- **Zone**, enter a zone label, keep in mind this label should match with your failure domain zone label `failure-domain.beta.kubernetes.io/zone`

- **Storage Class** is *optional*, it takes a comma delimited value, if not specified would default to standard, please make sure this storage class exists in your k8s cluster.

- **Kube Config** is *optional* if specified at provider level or else `required`

- **Namespace**, *optional* if provided SA have the `Cluster Admin` permissions else `required`. The SA used in provided `kubeconfig` should have access to this namespace.

<img title="K8s Configuration -- zone config" alt="K8s Configuration -- zone config" class="expandable-image" src="/images/ee/k8s-setup/k8s-az-kubeconfig.png" />

- `Overrides` is *optional*, if not specified Yugabyte Platform would use defaults specified inside the helm chart,

- Overrides to add Service level annotations

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

- Overrides to disable LoadBalancer

```yml
enableLoadBalancer: False
```

- Overrides to change the cluster domain name

```yml
domainName: my.cluster
```

- Overrides to add annotations at StatefulSet level

```yml
networkAnnotation:
  annotation1: 'foo'
  annotation2: 'bar'
```

- Overrides to add custom resource allocation for YB master & tserver pods & it overrides the instance types selected in the YB universe creation flow.

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

Add a new Zone by clicking **Add Zone** on the bottom left of the zone form.

Your form may have multiple AZ's as shown below.

<img title="K8s Configuration -- region" alt="K8s Configuration -- region" class="expandable-image" src="/images/ee/k8s-setup/k8s-add-region-flow.png" />

Click **Add Region** to add the region and close the modal.

Click **Save** to save the configuration. If successful, it will redirect you to the table view of all configurations.

## Next step

You are now ready to create YugabyteDB universes as outlined in the next section.
