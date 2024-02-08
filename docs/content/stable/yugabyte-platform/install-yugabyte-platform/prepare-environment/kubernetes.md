---
title: Prepare the Kubernetes environment
headerTitle: Cloud prerequisites
linkTitle: Cloud prerequisites
description: Prepare the Kubernetes environment for YugabyteDB Anywhere.
headContent: Prepare Kubernetes for YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: prepare-environment-4-kubernetes
    weight: 55
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws/" class="nav-link">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
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
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>
      OpenShift
    </a>
 </li>

  <li>
    <a href="../on-premises/" class="nav-link">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

</ul>

Preparing the environment involves a number of steps. Before you start, consult [Prerequisites for Kubernetes-based installations](../../prerequisites/kubernetes).

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

## Configure storage class

The type of volume provisioned for YugabyteDB Anywhere and YugabyteDB depends on the Kubernetes storage class being used. Consider the following recommendations when selecting or creating a storage class:

1. Set volume binding mode on a storage class to `WaitForFirstConsumer` for dynamically-provisioned volumes. This delays provisioning until a pod using the persistent volume claim (PVC) is created. The pod topology or scheduling constraints are respected.

   Scheduling might fail if the storage volume is not accessible from all the nodes in a cluster and the default volume binding mode is set to `Immediate` for certain regional cloud deployments. The volume may be created in a location or zone that is not accessible to the pod, causing the failure.

   For more information, see [Kubernetes: volume binding mode](https://kubernetes.io/docs/concepts/storage/storage-classes/#volume-binding-mode).

   On Google Cloud Provider (GCP), if you choose not to set binding mode to `WaitForFirstConsumer`, you might use regional persistent disks to replicate data between two zones in the same region on Google Kubernetes Engine (GKE). This can be used by the pod when it reschedules to another node in a different zone. For more information, see the following:
   - [Google Kubernetes Engine: persistent volumes and dynamic provisioning](https://cloud.google.com/kubernetes-engine/docs/concepts/persistent-volumes)
   - [Google Cloud: regional persistent disks](https://cloud.google.com/compute/docs/disks/high-availability-regional-persistent-disk)

1. Use an SSD-based storage class and an extent-based file system (XFS), as per recommendations provided in [Deployment checklist - Disks](../../../../deploy/checklist/#disks).

1. Set the `allowVolumeExpansion` to `true`. This enables you to expand the volumes later by performing additional steps if you run out of disk space. Note that some storage providers might not support this setting. For more information, see [Expanding persistent volumes claims](https://kubernetes.io/docs/concepts/storage/persistent-volumes/#expanding-persistent-volumes-claims).

The following is a sample storage class YAML file for Google Kubernetes Engine (GKE). You are expected to modify it to suit your Kubernetes cluster:

```yaml
apiVersion: storage.k8s.io/v1
kind: StorageClass
metadata:
  name: yb-storage
provisioner: kubernetes.io/gce-pd
volumeBindingMode: WaitForFirstConsumer
allowVolumeExpansion: true
parameters:
  type: pd-ssd
  fstype: xfs
```

## Specify ulimit and remember the location of core dumps

The core dump collection in Kubernetes requires special care due to the fact that `core_pattern` is not isolated in cgroup drivers.

You need to ensure that core dumps are enabled on the underlying Kubernetes node. Running the `ulimit -c` command within a Kubernetes pod or node must produce a large non-zero value or the `unlimited` value as an output. For more information, see [How to enable core dumps](https://www.ibm.com/support/pages/how-do-i-enable-core-dumps).

To be able to locate your core dumps, you should be aware of the fact that the location to which core dumps are written depends on the sysctl `kernel.core_pattern` setting. For more information, see [Linux manual: core dump file](https://man7.org/linux/man-pages/man5/core.5.html#:~:text=Naming).

To inspect the value of the sysctl within a Kubernetes pod or node, execute the following:

```sh
cat /proc/sys/kernel/core_pattern
```

If the value of `core_pattern` contains a `|` pipe symbol (for example, `|/usr/share/apport/apport -p%p -s%s -c%c -d%d -P%P -u%u -g%g -- %E` ), the core dump is being redirected to a specific collector on the underlying Kubernetes node, with the location depending on the exact collector. To be able to retrieve core dump files in case of a crash within the Kubernetes pod, it is important that you understand where these files are written.

If the value of `core_pattern` is a literal path of the form `/var/tmp/core.%p`, no action is required on your part, as core dumps will be copied by the YugabyteDB node to the persistent volume directory `/mnt/disk0/cores` for future analysis.

Note the following:

- ulimits and sysctl are inherited from Kubernetes nodes and cannot be changed for an individual pod.
- New Kubernetes nodes might be using [systemd-coredump](https://www.freedesktop.org/software/systemd/man/systemd-coredump.html) to manage core dumps on the node.

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
  helm search repo yugabytedb/yugaware --version {{<yb-version version="stable" format="short">}}
  ```

  You should see output similar to the following:

  ```output
  NAME                            CHART VERSION   APP VERSION     DESCRIPTION
  yugabytedb/yugaware             {{<yb-version version="stable" format="short">}}          {{<yb-version version="stable" format="build">}}    YugaWare is YugaByte Database's Orchestration a...
  ```

- Fetch images tag from `values.yaml` as tags may vary depending on the version.

  ```sh
  helm show values yugabytedb/yugaware --version {{<yb-version version="stable" format="short">}}
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
  docker pull quay.io/yugabyte/yugaware:{{<yb-version version="stable" format="build">}}
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
  docker tag quay.io/yugabyte/yugaware:{{<yb-version version="stable" format="build">}} us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/yugabyte/yugaware:{{<yb-version version="stable" format="build">}}
  docker tag nginxinc/nginx-unprivileged:1.23.3 us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/nginxinc/nginx-unprivileged:1.23.3
  docker tag postgres:14.4 us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/postgres:14.4
  docker tag tianon/postgres-upgrade:11-to-14 us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/tianon/postgres-upgrade:11-to-14
  docker tag prom/prometheus:v2.41.0 us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/prom/prometheus:v2.41.0
  ```

- Push images to the private container registry.

  Replace the Location, Project ID, Repository, and Image in the example as applicable.

  ```sh
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/yugabyte/yugaware:{{<yb-version version="stable" format="build">}}
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/nginxinc/nginx-unprivileged:1.23.3
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/postgres:14.4
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/tianon/postgres-upgrade:11-to-14
  docker push us-central1-docker.pkg.dev/yugabytedb-test-384308/yugabytepoc/prom/prometheus:v2.41.0
  ```

- Follow [Specify custom container registry](../../install-software/kubernetes/#specify-custom-container-registry) to install YugabyteDB Anywhere with the images from your private registry.
