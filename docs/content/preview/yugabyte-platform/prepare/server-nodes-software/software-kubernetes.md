---
title: YugabyteDB Anywhere Kubernetes software requirements
headerTitle: Software requirements for Kubernetes
linkTitle: Kubernetes provider
description: Software requirements for Kubernetes.
headContent: Prepare a Kubernetes cluster for YugabyteDB deployments
menu:
  preview_yugabyte-platform:
    identifier: software-kubernetes
    parent: server-nodes-software
    weight: 30
type: docs
---

## Software versions

The minimum software versions for a Kubernetes cluster and Helm chart are as follows:

- Kubernetes 1.22
- Helm 3.11.3

The YugabyteDB Anywhere Helm chart has been tested using the following software versions:

- Kubernetes - 1.22 to 1.25
- Helm - 3.11.3

For OpenShift clusters, the recommended OpenShift Container Platform (OCP) version is 4.6, with backward compatibility assumed but not guaranteed. In addition, ensure that you have the following:

- The latest oc binary in your path. For more information, see [Installing the OpenShift CLI](https://docs.openshift.com/container-platform/4.6/cli_reference/openshift_cli/getting-started-cli.html#installing-openshift-cli).
- A compatible kubectl binary in your path. See [Install and Set Up kubectl](https://kubernetes.io/docs/tasks/tools/install-kubectl/) for more information, or create a kubectl symlink pointing to oc.

## Specify ulimit and remember the location of core dumps

The core dump collection in Kubernetes requires special care due to the fact that `core_pattern` is not isolated in cgroup drivers.

You need to ensure that core dumps are enabled on the underlying Kubernetes node. Running the `ulimit -c` command within a Kubernetes pod or node must produce a large non-zero value or the `unlimited` value as an output. For more information, see [How to enable core dumps](https://www.ibm.com/support/pages/how-do-i-enable-core-dumps).

To be able to locate your core dumps, you should be aware of the fact that the location to which core dumps are written depends on the sysctl `kernel.core_pattern` setting. For more information, see [Linux manual: core dump file](https://man7.org/linux/man-pages/man5/core.5.html#:~:text=Naming).

To inspect the value of the sysctl within a Kubernetes pod or node, execute the following:

```sh
cat /proc/sys/kernel/core_pattern
```

If the value of `core_pattern` contains a `|` pipe symbol (for example, `|/usr/share/apport/apport -p%p -s%s -c%c -d%d -P%P -u%u -g%g -- %E` ), the core dump is being redirected to a specific collector on the underlying Kubernetes node, with the location depending on the exact collector. To be able to retrieve core dump files in case of a crash in the Kubernetes pod, it is important that you understand where these files are written.

If the value of `core_pattern` is a literal path of the form `/var/tmp/core.%p`, no action is required on your part, as core dumps will be copied by the YugabyteDB node to the persistent volume directory `/mnt/disk0/cores` for future analysis.

Note the following:

- ulimits and sysctl are inherited from Kubernetes nodes and cannot be changed for an individual pod.
- New Kubernetes nodes might be using [systemd-coredump](https://www.freedesktop.org/software/systemd/man/systemd-coredump.html) to manage core dumps on the node.

## Install kube-state-metrics

To be able to make use of the YugabyteDB Anywhere [node metrics](../../../alerts-monitoring/anywhere-metrics/), specifically the ones related to CPU, you need to install the [kube-state-metrics](https://github.com/kubernetes/kube-state-metrics) version 2.5 or later (2.8.1 is recommended) in your Kubernetes cluster.

The kube-state-metrics might already be installed and running. You should perform a check by executing the following command:

```sh
kubectl get svc kube-state-metrics -n kube-system
```

If the add-on is not installed, install it into the `kube-system` namespace by executing the following commands as a user of a service account with appropriate roles:

```sh
helm repo add prometheus-community https://prometheus-community.github.io/helm-charts
```

```sh
helm install -n kube-system --version 5.0.0 kube-state-metrics prometheus-community/kube-state-metrics
```

## Pull and push YugabyteDB Docker images to private container registry

Due to security concerns, some Kubernetes environments use internal container registries such as  Harbor and Nexus. In this type of setup, YugabyteDB deployment must be able to pull images from and push images to a private registry.

{{< note title="Note" >}}

This is not a recommended approach for enterprise environments. You should ask the container registry administrator to add proxy cache to pull the source images to the internal registry automatically. This would allow you to avoid modifying the Helm chart or providing a custom registry inside the YugabyteDB Anywhere cloud provider.

{{< /note >}}

Before proceeding, ensure that you have the following:

- Pull secret consisting of the user name and password or service account to access source (pull permission) and destination (push and pull permissions) container registries.
- Docker installed on a server (desktop) that can access both container registries. For installation instructions, see [Docker Desktop](https://www.docker.com/products/docker-desktop).

Generally, the process involves the following:

- Fetching the correct version of the YugabyteDB Helm chart whose `values.yaml` file describes all the image locations.
- Retagging images.
- Pushing images to the private container registry.
- Modifying the Helm chart values to point to the new private location.

![Pull and push YugabyteDB Docker images](/images/yp/docker-pull.png)

You need to perform the following steps:

- Log in to [quay.io](https://quay.io/) to access the YugabyteDB private registry using the user name and password provided in the secret YAML file. To find the `auth` field, use `base64 -d` to decode the data inside the `yaml` file twice. In this field, the user name and password are separated by a colon. For example, `yugabyte+<user-name>:ZQ66Z9C1K6AHD5A9VU28B06Q7N0AXZAQSR`.

  ```sh
  docker login -u "your_yugabyte_username" -p "yugabyte_provided_password" quay.io
  docker search quay.io/yugabyte
  ```

- Install Helm to fetch the YugabyteDB Anywhere Helm chart on your machine. For more information, refer to [Installing Helm](https://helm.sh/docs/intro/install/).

- Run the following `helm repo add` command to clone the [YugabyteDB charts repository](https://charts.yugabyte.com/).

  ```sh
  helm repo add yugabytedb https://charts.yugabyte.com
  helm repo update
  ```

- Run the following command to search for the version.

  ```sh
  helm search repo yugabytedb/yugaware --version {{<yb-version version="preview" format="short">}}
  ```

  You should see output similar to the following:

  ```output
  NAME                            CHART VERSION   APP VERSION     DESCRIPTION
  yugabytedb/yugaware             {{<yb-version version="preview" format="short">}}          {{<yb-version version="preview" format="build">}}    YugaWare is YugaByte Database's Orchestration a...
  ```

- Fetch images tag from `values.yaml` as tags may vary depending on the version.

  ```sh
  helm show values yugabytedb/yugaware --version {{<yb-version version="preview" format="short">}}
  ```

  You should see output similar to the following:

  ```properties
  image:
    commonRegistry: ""

    repository: quay.io/yugabyte/yugaware
    tag: 2.16.1.0-b50
    pullPolicy: IfNotPresent
    pullSecret: yugabyte-k8s-pull-secret

    thirdparty-deps:
      registry: quay.io
      tag: latest
      name: yugabyte/thirdparty-deps

    postgres:
      registry: ""
      tag: '14.4'
      name: postgres

    postgres-upgrade:
      registry: ""
      tag: "11-to-14"
      name: tianon/postgres-upgrade

    prometheus:
      registry: ""
      tag: v2.41.0
      name: prom/prometheus

    nginx:
      registry: ""
      tag: 1.23.3
      name: nginxinc/nginx-unprivileged
  ...
  ```

- Pull images to your machine.

  These image tags will vary based on the version.

  ```sh
  docker pull quay.io/yugabyte/yugaware:{{<yb-version version="preview" format="build">}}
  docker pull postgres:14.4
  docker pull prom/prometheus:v2.41.0
  docker pull tianon/postgres-upgrade:11-to-14
  docker pull nginxinc/nginx-unprivileged:1.23.3
  ```

- Log in to your target container registry, as per the following example that uses Google Artifact Registry.

  Replace the Service Account and Location in the example as applicable.

  ```sh
  gcloud auth activate-service-account yugabytedb-test@yugabytedb-test-384308.iam.gserviceaccount.com --key-file=key.json
  gcloud auth configure-docker us-central1-docker.pkg.dev
  
  ```

- Tag the local images to your target registry. The following example uses Google Artifact Registry.

  Replace the Location, Project ID, Repository, and Image in the example as applicable.

  ```sh
  docker tag quay.io/yugabyte/yugaware:{{<yb-version version="preview" format="build">}} us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/yugabyte/yugaware:{{<yb-version version="preview" format="build">}}
  docker tag nginxinc/nginx-unprivileged:1.23.3 us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/nginxinc/nginx-unprivileged:1.23.3
  docker tag postgres:14.4 us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/postgres:14.4
  docker tag tianon/postgres-upgrade:11-to-14 us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/tianon/postgres-upgrade:11-to-14
  docker tag prom/prometheus:v2.41.0 us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/prom/prometheus:v2.41.0
  ```

- Push images to the private container registry.

  Replace the Location, Project ID, Repository, and Image in the example as applicable.

  ```sh
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/yugabyte/yugaware:{{<yb-version version="preview" format="build">}}
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/nginxinc/nginx-unprivileged:1.23.3
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/postgres:14.4
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/tianon/postgres-upgrade:11-to-14
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/prom/prometheus:v2.41.0
  ```

- Follow [Specify custom container registry](../../../install-yugabyte-platform/install-software/kubernetes/#specify-custom-container-registry) to install YugabyteDB Anywhere with the images from your private registry.
