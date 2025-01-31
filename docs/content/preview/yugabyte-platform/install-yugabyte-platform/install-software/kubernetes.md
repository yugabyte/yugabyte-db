---
title: Install YugabyteDB Anywhere software - Kubernetes
headerTitle: Install YugabyteDB Anywhere
linkTitle: Install YBA software
description: Install YugabyteDB Anywhere software in your Kubernetes environment.
headContent: Install YBA software in a Kubernetes environment
menu:
  preview_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: install-software-2-kubernetes
    weight: 10
type: docs
---

For higher availability, you can install additional YugabyteDB Anywhere (YBA) instances, and configure them later to serve as passive warm standby servers. See [Enable High Availability](../../../administer-yugabyte-platform/high-availability/) for more information.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../installer/" class="nav-link">
      <i class="fa-solid fa-building"></i>On-premises and public clouds</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

For information on installing YugabyteDB Anywhere on OpenShift, refer to [Install YugabyteDB Anywhere on OpenShift](../openshift/).

## Install YugabyteDB Anywhere

You install YugabyteDB Anywhere on a Kubernetes cluster as follows:

1. Create a namespace by executing the following `kubectl create namespace` command:

    ```sh
    kubectl create namespace yb-platform
    ```

1. Apply the YugabyteDB Anywhere secret that you obtained from Yugabyte Support by running the following `kubectl create` command:

    ```sh
    kubectl create -f yugabyte-k8s-secret.yml -n yb-platform
    ```

    Expect the following output notifying you that the secret was created:

    ```output
    secret/yugabyte-k8s-pull-secret created
    ```

1. Run the following `helm repo add` command to clone the [YugabyteDB charts repository](https://charts.yugabyte.com/):

    ```sh
    helm repo add yugabytedb https://charts.yugabyte.com
    ```

    A message similar to the following should appear:

    ```output
    "yugabytedb" has been added to your repositories
    ```

    To search for the available chart version, run the following command:

    ```sh
    helm search repo yugabytedb/yugaware --version {{<yb-version version="preview" format="short">}}
    ```

    The latest Helm chart version and application version is displayed via the output similar to the following:

    ```output
    NAME                 CHART VERSION  APP VERSION  DESCRIPTION
    yugabytedb/yugaware {{<yb-version version="preview" format="short">}}          {{<yb-version version="preview" format="build">}}  YugaWare is YugaByte Database's Orchestration a...
    ```

1. Run the following `helm install` command to install the YugabyteDB Anywhere (`yugaware`) Helm chart:

    ```sh
    helm install yw-test yugabytedb/yugaware --version {{<yb-version version="preview" format="short">}} -n yb-platform --wait
    ```

    You can enable TLS by following instructions provided in [Configure TLS](#configure-tls).

    To install YugabyteDB Anywhere using the Yugabyte Kubernetes Operator (the feature is in [Tech Preview](/preview/releases/versioning/#feature-maturity)), see [Use Yugabyte Kubernetes Operator to automate YugabyteDB Anywhere deployments](#use-yugabyte-kubernetes-operator-to-automate-yba-deployments).

1. Use the following command to check the service:

    ```sh
    kubectl get svc -n yb-platform
    ```

    The following output should appear:

    ```output
    NAME                  TYPE           CLUSTER-IP     EXTERNAL-IP    PORT(S)                       AGE
    yw-test-yugaware-ui   LoadBalancer   10.111.241.9   34.93.169.64   80:32006/TCP,9090:30691/TCP   2m12s
    ```

1. Use the following command to check that all the pods have been initialized and are running:

    ```sh
    kubectl get pods -n yb-platform
    ```

    The following output should appear:

    ```output
    NAME                 READY   STATUS    RESTARTS   AGE
    yw-test-yugaware-0   4/4     Running   0          12s
    ```

    Note that even though the preceding output indicates that the `yw-test-yugaware-0` pod is running, it does not mean that YugabyteDB Anywhere is ready to accept your queries. If you open the load balancer IP `34.93.169.64:80` and see an error (such as 502), it means that `yugaware` is still being initialized. You can check readiness of `yugaware` by executing the following command:

    ```sh
    kubectl logs --follow -n yb-platform yw-test-yugaware-0 yugaware
    ```

    An output similar to the following would confirm that there are no errors and that the server is running:

    ```output
    [info] AkkaHttpServer.scala:447 [main] Listening for HTTP on /0.0.0.0:9000
    ```

    If YugabyteDB Anywhere fails to start for the first time, verify that your system meets the installation requirements, as per [Hardware requirements (Kubernetes)](../../../prepare/server-nodes-hardware/). See [Install and upgrade issues on Kubernetes](../../../troubleshoot/install-upgrade-issues/kubernetes/) to troubleshoot the problem.

## Customize YugabyteDB Anywhere

You can customize YugabyteDB Anywhere on a Kubernetes cluster in a number of ways, such as by specifying the values on CLI or passing a YAML file to the `helm install` command, as follows:

```sh
helm install yw-test yugabytedb/yugaware \
  --version {{<yb-version version="preview" format="short">}} \
  -n yb-platform \
  --values yba-values.yaml \
  --wait
```

You can copy the preceding code block into a file called `yba-values.yaml` and then install YugabyteDB Anywhere using this command. Alternatively, you can pass the values using the `--set key=value` flag. For more information, see [Customizing the chart before installing](https://helm.sh/docs/intro/using_helm/#customizing-the-chart-before-installing). It is recommend to use a values file and store it in a version control system.

If you are looking for a customization which is not listed, you can view all the supported options and their default values by running the `helm show values yugabytedb/yugaware --version {{<yb-version version="preview" format="short">}}` command and copying the specific section to your own values file.

### Use Yugabyte Kubernetes Operator to automate YBA deployments

The [Yugabyte Kubernetes Operator](../../../anywhere-automation/yb-kubernetes-operator/) {{<tags/feature/tp>}} automates the deployment, scaling, and management of YugabyteDB clusters in Kubernetes environments.

Note that for Yugabyte Kubernetes Operator to work correctly, you need to set `rbac.create=true`, as the operator needs ClusterRoles to create its own providers.

To install YugabyteDB Anywhere and a universe using the Yugabyte Kubernetes Operator, do the following:

1. Apply the following Custom Resource Definition:

    ```sh
    kubectl apply -f https://raw.github.com/yugabyte/charts/2024.1/crds/concatenated_crd.yaml
    ```

1. Run the following `helm install` command to set the parameters from the preceding YAML file to install the YugabyteDB Anywhere (`yugaware`) Helm chart:

    ```sh
    # Modify the fields kubernetesOperatorNamespace and defaultUser.password fields as required
    helm install yugabytedb/yugaware \
      --version 2024.1.0 \
      --set kubernetesOperatorEnabled=true,kubernetesOperatorNamespace="yb-platform-test",defaultUser.enabled=true,defaultUser.password="Password#Test123"–generate-name
    ```

1. Verify that YBA is up, and the Kubernetes Operator is installed successfully using the following commands:

    ```sh
    kubectl get pods -n <yba_namespace>
    ```

    ```sh
    kubectl get pods -n <operator_namespace>
    ```

    ```output
    NAME                                       READY   STATUS    RESTARTS   AGE
    chart-1706728534-yugabyte-k8s-operator-0   3/3     Running   0          26h
    ```

    Additionally, you should see no stack traces, but the following messages in the `KubernetesOperatorReconciler` log:

    ```output
    LOG.info("Finished running ybUniverseController");
    ```

1. Create the following custom resource, and save it as `demo-universe.yaml`.

    ```yaml
    # demo-universe.yaml
    apiVersion: operator.yugabyte.io/v1alpha1
    kind: YBUniverse
    metadata:
      name: demo-test
    spec:
      numNodes: 1
      replicationFactor: 1
      enableYSQL: true
      enableNodeToNodeEncrypt: true
      enableClientToNodeEncrypt: true
      enableLoadBalancer: true
      ybSoftwareVersion: "2024.1.0-b2" <- This will be the YBA  version
      enableYSQLAuth: false
      enableYCQL: true
      enableYCQLAuth: false
      gFlags:
        tserverGFlags: {}
        masterGFlags: {}
      deviceInfo:
        volumeSize: 100
        numVolumes: 1
        storageClass: "yb-standard"
    ```

1. Create a universe using the custom resource `demo-universe.yaml` as follows:

    ```sh
    kubectl apply -f demo-universe.yaml -n yb-platform
    ```

1. Check the status of the universe as follows:

    ```sh
    kubectl get ybuniverse  -n yb-operator
    ```

    ```output
    NAME                STATE   SOFTWARE VERSION
    anab-test-2         Ready   2.23.0.0-b33
    anab-test-backups   Ready   2.21.1.0-b269
    anab-test-restore   Ready   2.21.1.0-b269
    ```

For more details, see [Yugabyte Kubernetes Operator](../../../anywhere-automation/yb-kubernetes-operator/).

### Customize the creation of an internal service account

By default, the Helm chart will attempt to create a service account that has certain ClusterRoles listed [here](https://github.com/yugabyte/charts/blob/master/stable/yugaware/templates/rbac.yaml#L166). These roles are used to do the following:

1. Enable YBA to collect resource metrics such as CPU and memory from the Kubernetes nodes.
1. Create YugabyteDB deployments in new namespaces.

To customize this behavior (and potentially lose some functionality), you can set the `serviceAccount` value to a pre-existing service account that you've already created. It is recommended that you at least grant this service account the cluster roles listed in the "required to scrape" section of the [RBAC configuration file](https://github.com/yugabyte/charts/blob/master/stable/yugaware/templates/rbac.yaml#L166), along with a namespace admin role. To completely disable this behavior, set the `rbac.create` value to false. Note that without the ability to create new namespaces, YugabyteDB Anywhere must be [configured with a pre-created namespace](../../../configure-yugabyte-platform/kubernetes/#configure-region-and-zones).

### Specify custom container registry

If you have pushed the container images to a custom registry as mentioned in [Pull and push YugabyteDB Docker images to private container registry](../../../prepare/server-nodes-software/software-kubernetes/#pull-and-push-yugabytedb-docker-images-to-private-container-registry), set the registry address, as follows:

```yaml
# yba-values.yaml
image:
  commonRegistry: "gcr.io/mycustomregistry"
```

If the registry requires authentication, then create a pull secret and pass the name as follows:

```yaml
# yba-values.yaml
image:
  commonRegistry: "gcr.io/mycustomregistry"
  pullSecret: "mycustomregistry-pull-secret"
```

### Configure load balancer

By default, a load balancer is created to enable access to YugabyteDB Anywhere. You can configure an internal load balancer, configure a DNS name, or disable the load balancer altogether.

#### Disable the load balancer

To access YugabyteDB Anywhere by other means (such as port-forwarding, different gateway or ingress solutions, and so on) you can disable the load balancer by changing the service type to `ClusterIP`, as follows:

```yaml
# yba-values.yaml
yugaware:
  # other values…
  service:
    type: "ClusterIP"
```

If you plan to access YugabyteDB Anywhere via port-forwarding, you need to set `tls.hostname`, as follows:

```yaml
# yba-values.yaml
tls:
  hostname: "localhost:8080"
```

For more information, see [Set a DNS name](#set-a-dns-name).

Use the kubectl `port-forward` command to access the interface locally, as follows:

```sh
# For TLS. Available at https://localhost:8080
kubectl port-forward -n yb-platform svc/yw-test-yugaware-ui 8080:443

# For non-TLS. Available at http://localhost:8080
kubectl port-forward -n yb-platform svc/yw-test-yugaware-ui 8080:80
```

#### Set up an internal load balancer

You can add annotations to the YugabyteDB Anywhere service to create an internal load balancer instead of a publicly-accessible one. Because every cloud provider has different annotations for doing this, refer to the following documentation:

- For Google Cloud, see [Internal load balancing in GKE](https://cloud.google.com/kubernetes-engine/docs/how-to/internal-load-balancing).
- For Azure, see [Internal load balancer in AKS](https://docs.microsoft.com/en-us/azure/aks/internal-lb).
- For AWS, see [EKS load balancing](https://docs.aws.amazon.com/eks/latest/userguide/load-balancing.html) and [AWS load balancer controller](https://kubernetes-sigs.github.io/aws-load-balancer-controller/latest/guide/service/annotations/#lb-scheme).
- For other providers, see [Internal load balancer](https://kubernetes.io/docs/concepts/services-networking/service/#internal-load-balancer).

For example, for a GKE cluster, you would add the following to your values file:

```yaml
# yba-values.yaml
yugaware:
  service:
    # other values…
    annotations:
      networking.gke.io/load-balancer-type: "Internal"
```

For an EKS cluster, you would use the following:

```yaml
# yba-values.yaml
yugaware:
  service:
    # other values…
    annotations:
      # used by builtin load balancer controller of Kubernetes
      service.beta.kubernetes.io/aws-load-balancer-internal: "true"
      # required if using AWS load balancer controller
      service.beta.kubernetes.io/aws-load-balancer-scheme: "internal"
```

#### Set a DNS name

If you want to access YugabyteDB Anywhere via a domain or localhost, you need to set the `tls.hostname` field to ensure that the correct Transport Layer Security (TLS) and Cross-Origin Resource Sharing (CORS) settings are used, as follows:

```yaml
# yba-values.yaml
tls:
  hostname: "yba.example.com"
```

Similarly, if you want to access YugabyteDB Anywhere from multiple domains or you have a complex reverse-proxy setup, you can add those domains to CORS configuration, as follows:

```yaml
# yba-values.yaml
yugaware:
  # other values…
  additionAllowedCorsOrigins:
  - "yba-east.example.com"
  - "yba-test.example.com"
```

### Configure TLS

You can configure YugabyteDB Anywhere to use TLS.

#### Enable TLS

Add the following lines to your values file to enable TLS:

```yaml
# yba-values.yaml
tls:
  enabled: true
```

The Helm chart will create a self-signed certificate for you.

#### Use a custom TLS certificate

You can use a custom TLS certificate instead of using the default self-signed certificate. Set the value of `certificate` and `key` to the base64-encoded string value of the certificate and the key, as follows:

```yaml
# yba-values.yaml
tls:
  enabled: true
  certificate: "LS0tLS1CRUdJTiBDRVJUSUZJQ..."
  key: "LS0tLS1CRUdJTiBQUklWQVRFIEtFWS0t..."
```

#### Change TLS versions

When using TLS with YugabyteDB Anywhere, you can change the supported TLS versions, as follows:

```yaml
# yba-values.yaml
tls:
  enabled: true
  # other values…
  sslProtocols: "TLSv1.2 TLSv1.3"
```

The value is passed to Nginx frontend as [ssl_protocols](https://nginx.org/r/ssl_protocols) operational directive.

### Control placement of YugabyteDB Anywhere pods

The Helm chart allows you to control the placement of the pods when installing YugabyteDB Anywhere in your Kubernetes cluster via `nodeSelector`, `zoneAffinity`, and `toleration`. When you are using these constraints, ensure that you are following recommendations provided in [Hardware requirements](../../../prepare/server-nodes-hardware/). For more information about pod placement, see [Assigning pods to nodes](https://kubernetes.io/docs/concepts/scheduling-eviction/assign-pod-node/).

#### nodeSelector

The Kubernetes `nodeSelector` field provides the means to constrain pods to nodes with specific labels, allowing you to restrict the placement of YugabyteDB Anywhere pods on a particular node, as demonstrated by the following example:

```yaml
# yba-values.yaml
nodeSelector:
  kubernetes.io/hostname: "node-name-1"
```

#### zoneAffinity

Kubernetes provides a flexible `nodeAffinity` construct to constrain the placement of pods to nodes in a given zone.

When your Kubernetes cluster nodes are spread across multiple zones, you can use this command to explicitly place the YugabyteDB Anywhere pods on specific zones, as demonstrated by the following example:

```yaml
# yba-values.yaml
zoneAffinity:
- us-west1-a
- us-west1-b
```

#### tolerations

Kubernetes nodes could have taints that repel pods from being placed on it. Only pods with a toleration for the same taint are permitted. For more information, see [Taints and Tolerations](https://kubernetes.io/docs/concepts/scheduling-eviction/taint-and-toleration/).

For example, if some of the nodes in your Kubernetes cluster are earmarked for experimentation and have a taint `dedicated=experimental:NoSchedule`, only pods with the matching toleration will be allowed, whereas other pods will be prevented from being placed on these nodes:

```yaml
# yba-values.yaml
tolerations:
- key: "dedicated"
  operator: "Equal"
  value: "experimental"
  effect: "NoSchedule"
```

Note that tolerations do not guarantee scheduling on the tainted nodes. To ensure that the YugabyteDB Anywhere pods use a dedicated set of nodes, you need to use [nodeSelector](#nodeselector) along with taints and tolerations to repel other pods.

### Modify resources

You can modify the resource requests and limits set for the various components of YugabyteDB Anywhere, including CPU and memory resources, as follows:

```yaml
# yba-values.yaml
yugaware:
  # other values…
  resources:
    requests:
      cpu: "4"
      memory: "5Gi"
    # optionally set limits
    limits:
      cpu: "5"
      memory: "8Gi"
```

For more information, see [Memory resources](https://kubernetes.io/docs/tasks/configure-pod-container/assign-memory-resource/) and [CPU resources](https://kubernetes.io/docs/tasks/configure-pod-container/assign-cpu-resource/).

Similarly, you can modify the values for Prometheus and PostgreSQL containers which are part of the chart, as follows:

```yaml
# yba-values.yaml
prometheus:
  # other values…
  resources:
    requests:
      cpu: "2"
      memory: "6Gi"

postgres:
  # other values…
  resources:
    requests:
      cpu: "1.5"
      memory: "2Gi"
```

### Run containers as non-root

The PostgreSQL and Nginx containers always run as non-root. To run the rest of the containers as non-root, you can set the following values:

```yaml
securityContext:
  enabled: true
```

This value is not supported on OpenShift, which runs all the containers of YugabyteDB Anywhere as non-root by default. Modifying securityContext on OpenShift could cause the containers to fail.

### Set pod labels and annotations

Kubernetes resources, such as pods, can have additional metadata in the form of labels and annotations. These key-value pairs are used by other tools such as Prometheus. You can add labels and annotations to the YugabyteDB Anywhere pods as follows:

```yaml
# yba-values.yaml
yugaware:
  # other values…
  pod:
    annotations:
      sidecar.istio.io/proxyCPU: "200m"
    labels:
      sidecar.istio.io/inject: true
      prometheus.io/scrape: true
```

### Specify custom storage class

The storage class used by YugabyteDB Anywhere pods can be changed, along with the size of the volume, by using the following values:

```yaml
# yba-values.yaml
yugaware:
  storageClass: "custom-sc"
  storage: "200Gi"
```

You should use a storage class that has been configured based on recommendations provided in [Hardware requirements (Kubernetes)](../../../prepare/server-nodes-hardware/).

In addition, it is recommended to set a large initial storage size, because resizing the volumes later is challenging.

<!-- TODO: update this when we revisit the "Pull and push YugabyteDB Docker images to private container registry" section as part of PLAT-6797  -->
<!-- ### Pull images from private registry -->

## Enable GKE service account-based IAM

If you are using Google Cloud Storage (GCS) for backups, you can enable GKE service account-based IAM (GCP IAM) so that Kubernetes universes can access GCS.

Before enabling GCP IAM, ensure you have the prerequisites. Refer to [GCP IAM](../../../back-up-restore-universes/configure-backup-storage/#gke-service-account-based-iam-gcp-iam).

To enable GCP IAM, provide the following additional Helm values during installation to a version which supports this feature (v2.18.4 or later):

- serviceAccount: Provide the name of the Kubernetes service account you created. Note that this service account should be present in the namespace being used for the YugabyteDB pod resources.
- [nodeSelector](#nodeselector): Pass a node selector override to make sure YBA pods are scheduled on the GKE cluster's worker nodes which have a metadata server running.

    ```yaml
    yugaware:
    ....serviceAccount: <KSA_NAME>
    nodeSelector:
    ....iam.gke.io/gke-metadata-server-enabled: "true"
    ```

## Delete the Helm installation of YugabyteDB Anywhere

To delete the Helm installation, run the following command:

```sh
helm uninstall yw-test -n yb-platform
```
