---
title: Install Yugabyte Platform software - Kubernetes
headerTitle: Install Yugabyte Platform software - Kubernetes
linkTitle: Install software 
description: Install Yugabyte Platform software in your Kubernetes environment.
menu:
  latest:
    parent: install-yugabyte-platform
    identifier: install-software-2-kubernetes
    weight: 77
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/default" class="nav-link">
      <i class="fas fa-cloud"></i>
      Default
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/kubernetes" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li >
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/airgapped" class="nav-link">
      <i class="fas fa-unlink"></i>
      Airgapped
    </a>
  </li>

<li >
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/openshift" class="nav-link">
      <i class="fas fa-cubes"></i> OpenShift </a>
  </li>

</ul>

## Prerequisites

Before you install Yugabyte Platform on a Kubernetes cluster, perform the following:

- Create a yugabyte-helm service account.
- Create a `kubeconfig` file for configuring access to the Kubernetes cluster.

### Create a yugabyte-helm service account

Run the following `kubectl` command to apply the YAML file:

```sh
kubectl apply -f https://raw.githubusercontent.com/YugaByte/charts/master/stable/yugabyte/yugabyte-rbac.yaml
```

The following output should appear:

```
serviceaccount "yugabyte-helm" created
clusterrolebinding "yugabyte-helm" created
```

## Create a `kubeconfig` File for a Kubernetes Cluster

You can create a `kubeconfig` file for a yugabyte-helm service account as follows:

1. Run the following `wget` command to get the Python script for generating the `kubeconfig` file:

    ```sh
    wget https://raw.githubusercontent.com/YugaByte/charts/master/stable/yugabyte/generate_kubeconfig.py
    ```

2. Run the following command to generate the `kubeconfig` file:

    ```sh
    python generate_kubeconfig.py -s yugabyte-helm
    ```
    
    The following output should appear:
    
    ```
    Generated the kubeconfig file: /tmp/yugabyte-helm.conf
    ```
    
3. Upload the generated `kubeconfig` file as the `kubeconfig` in the Yugabyte Platform provider configuration.

## Install Yugabyte Platform on a Kubernetes Cluster

You install Yugabyte Platform on a Kubernetes cluster as follows:

1. Create a namespace by executing the following `kubectl create namespace` command:

    ```sh
    kubectl create namespace yb-platform
    ```

2. Apply the Yugabyte Platform secret that you obtained from [Yugabyte](https://www.yugabyte.com/platform/#request-trial-form) by running the following `kubectl create` command:

    ```sh
    $ kubectl create -f yugabyte-k8s-secret.yml -n yb-platform
    ```

    Expect the following output notifying you that the secret was created:

    ```
    secret/yugabyte-k8s-pull-secret created
    ```

3. Run the following `helm repo add` command to clone the [YugabyteDB charts repository](https://charts.yugabyte.com/):

    ```sh
    $ helm repo add yugabytedb https://charts.yugabyte.com
    ```

    A message similar to the following should appear:

    ```
    "yugabytedb" has been added to your repositories
    ```

    To search for the available chart version, run this command:

    ```sh
    $ helm search repo yugabytedb/yugaware -l
    ```

    The latest Helm Chart version and App version will be displayed:

    ```
    NAME               	CHART VERSION	APP VERSION	DESRIPTION
    yugabytedb/yugabyte	2.3.3        	2.3.3.0	YugabyteDB is the high-performance distributed ..
    ```

4. Run the following `helm install` command to install Yugabyte Platform (`yugaware`) Helm chart:

    ```sh
    helm install yw-test yugabytedb/yugaware --version 2.3.3 -n yb-platform --wait
    ```

5. Optionally, set the TLS version for Nginx frontend by using `ssl_protocols` operational directive in the Helm installation, as follows:

    ```sh
    helm install yw-test yugabytedb/yugaware --version 2.3.3 -n yb-platform --wait --set tls.sslProtocols="TLSv1.2"
    ```

A message output will notify you whether or not the deployment is successful.

## Delete the Helm Installation of Yugabyte Platform

To delete the Helm installation, run the following `helm del` command:

```sh
helm del --purge yw-test -n yb-platform
```
