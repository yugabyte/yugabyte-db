---
title: Cloud setup for deploying YugabyteDB Anywhere
headerTitle: To deploy nodes
linkTitle: To deploy nodes
description: Prepare your cloud for deploying YugabyteDB universe nodes.
headContent: Prepare your cloud for deploying YugabyteDB universe nodes
menu:
  preview_yugabyte-platform:
    identifier: cloud-permissions-nodes-5-k8s
    parent: cloud-permissions
    weight: 20
type: docs
---

For YugabyteDB Anywhere (YBA) to be able to deploy and manage YugabyteDB clusters, you need to provide YBA with privileges on your cloud infrastructure to create, delete, and modify VMs, mount and unmount disk volumes, and so on.

The more permissions that you can provide, the more YBA can automate.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../cloud-permissions-nodes/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-aws/" class="nav-link">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-gcp" class="nav-link">
      <i class="fa-brands fa-google"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-azure" class="nav-link">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-k8s" class="nav-link active">
      <i class="fa-regular fa-dharmachakra"></i>
      Kubernetes
    </a>
  </li>
</ul>

## Kubernetes

As a prerequisite for creating pods and deploying database clusters, YBA requires a service account in the Kubernetes cluster and a corresponding kubeconfig file.

Do one of the following:

- Create a [`yugabyte-platform-universe-management` service account](#create-a-service-account) and a [`kubeconfig` file](#create-a-kubeconfig-file) directly.

  Do this for each Kubernetes cluster if you are doing a multi-cluster setup.

- If deploying into a single Kubernetes cluster, have YBA auto-fill these values using the existing service account and kubeconfig file used for your YBA installation.

  See [Create a provider](../../../configure-yugabyte-platform/kubernetes/) for details.

### Create a service account

The secret of a service account can be used to generate a `kubeconfig` file. This account should not be deleted once it is in use by YBA.

Set the `YBA_NAMESPACE` environment variable to the namespace where your YBA is installed, as follows:

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

For example, if your goal is to allow YBA to manage YugabyteDB universes in the namespaces `yb-db-demo` and `yb-db-us-east4-a` (the target namespaces), then you need to apply in both the target namespaces, as follows:

```sh
export YBA_NAMESPACE="yb-platform"

curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-namespaced.yaml \
  | sed "s/namespace: <SA_NAMESPACE>/namespace: ${YBA_NAMESPACE}"/g \
  | kubectl apply -n ${YBA_NAMESPACE} -f -
```

### Create a kubeconfig file

You can create a `kubeconfig` file for the previously created `yugabyte-platform-universe-management` service account as follows:

1. Run the following `wget` command to get the Python script for generating the `kubeconfig` file:

    ```sh
    wget https://raw.githubusercontent.com/YugaByte/charts/master/stable/yugabyte/generate_kubeconfig.py
    ```

1. Run the following command to generate the `kubeconfig` file:

    ```sh
    export YBA_NAMESPACE="yb-platform"

    python generate_kubeconfig.py -s yugabyte-platform-universe-management -n ${YBA_NAMESPACE}
    ```

    Expect the following output:

    ```output
    Generated the kubeconfig file: /tmp/yugabyte-platform-universe-management.conf
    ```

1. Use this generated `kubeconfig` file for your Kubernetes provider configuration.

| Save for later | To configure |
| :--- | :--- |
| kubeconfig file | [Kubernetes provider](../../../configure-yugabyte-platform/kubernetes/) |
