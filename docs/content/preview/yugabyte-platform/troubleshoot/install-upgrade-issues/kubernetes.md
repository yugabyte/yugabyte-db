---
title: Install and upgrade issues on Kubernetes
headerTitle: Install and upgrade issues on Kubernetes
linkTitle: Install and upgrade issues
description: Troubleshoot issues encountered when installing or upgrading YugabyteDB Anywhere on Kubernetes.
menu:
  preview_yugabyte-platform:
    identifier: install-upgrade-kubernetes-issues
    parent: troubleshoot-yp
    weight: 13
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

 <li>
    <a href="../vm/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      Virtual machine </a>
  </li>  

<li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li> 

</ul>

Occasionally, you might encounter issues during installation and upgrade of YugabyteDB Anywhere on Kubernetes. You can troubleshoot most of these issues. 

If you experience difficulties while troubleshooting, contact {{% support-platform %}}.

For more information, see the following:

- [Debug pods](https://kubernetes.io/docs/tasks/debug/debug-application/debug-pods/)
- [Debug running pods](https://kubernetes.io/docs/tasks/debug/debug-application/debug-running-pod/)
- [Debug services](https://kubernetes.io/docs/tasks/debug/debug-application/debug-service/)
- [Kubernetes troubleshooting guide for applications](https://kubernetes.io/docs/tasks/debug/debug-application/)

## Pod scheduling failure

YugabyteDB Anywhere pod scheduling can fail for a variety of reasons, such as insufficient resource allocation, mismatch in the node selector or affinity, incorrect storage class configuration, problems with Elastic Block Store (EBS). Typically, this manifests by pods being in a pending state for a long time.

For additional information, see [Node selection in kube-scheduler](https://kubernetes.io/docs/concepts/scheduling-eviction/kube-scheduler/#kube-scheduler-implementation).

### Insufficient resources

To start diagnostics, execute the following command to obtain the pod:

```sh
kubectl get pod -n <NAMESPACE>
```
If the issue you are experiencing is due to the pod scheduling failure, expect to see `STATUS` as `Pending`, as per the following output:

```output
NAME                 READY   STATUS    RESTARTS   AGE
yw-test-yugaware-0   0/4     Pending   0          2m30s
```

Execute the following command to obtain detailed information about the pod and failure:

```sh
kubectl describe pod <POD_NAME> -n <NAMESPACE>
```
Expect to see a Message similar to the following:

```output
Events:
  Type     Reason             Age                From                Message
  ----     ------             ----               ----                -------
  Warning  FailedScheduling   56s                default-scheduler   0/2 nodes are available: 2 Insufficient cpu.
```

For more information, see [Kubernetes: Specify a CPU request that is too big for your nodes](https://kubernetes.io/docs/tasks/configure-pod-container/assign-cpu-resource/#specify-a-cpu-request-that-is-too-big-for-your-nodes).

**Resolution**

- Ensure that you have enough resources in the Kubernetes cluster to schedule the YugabyteDB Anywhere pods. For more information, see [Prerequisites - Kubernetes](../../../install-yugabyte-platform/prerequisites/kubernetes/#hardware-requirements).
- Modify the YugabyteDB Anywhere pods resources configuration. For more information, see [Modify resources](../../../install-yugabyte-platform/install-software/kubernetes/#modify-resources).

### Mismatch in node selector, affinity, taints, tolerations

To start diagnostics, execute the following command to obtain detailed information about the failure:

```sh
kubectl describe pod <POD_NAME> -n <NAMESPACE>
```
If the issue you are experiencing is due to the mismatched node selector or affinity, expect to see a Message similar to the following:

```output
Events:
  Type     Reason             Age                From                Message
  ----     ------             ----               ----                -------
Warning  FailedScheduling   75s (x40 over 55m)    default-scheduler   0/55 nodes are available: 19 Insufficient cpu, 36 node(s) didn't match Pod's node affinity/selector
```

**Resolution**

Ensure that there is no mismatch between labels or taints when you schedule YugabyteDB Anywhere pods on specific nodes. Otherwise, the scheduler can fail to identify the node. For more information, see the following:

- [Node selector](../../../install-yugabyte-platform/install-software/kubernetes/#nodeselector)
- [Affinity](../../../install-yugabyte-platform/install-software/kubernetes/#zoneaffinity)
- [Taints and tolerations](../../../install-yugabyte-platform/install-software/kubernetes/#tolerations)

### Storage class VolumeBindingMode is not set to WaitForFirstConsumer

During multi-zone deployment of YugabyteDB, start diagnostsics by executing the following command to obtain detailed information about the failure:

```sh
kubectl describe pod <POD_NAME> -n <NAMESPACE>
```
If the issue you are experiencing is due to the incorrect setting for the storage class `VolumeBindingMode`, expect to see a Message similar to the following:

```output
Events:
  Type     Reason             Age                From                Message
  ----     ------             ----               ----                -------
Warning  FailedScheduling   75s (x40 over 55m)    default-scheduler   0/55 nodes are available: 19 Insufficient cpu, 36 node(s) didn't match Pod's node affinity/selector
```

You can obtain information related to storage classes, as follows:

- Get the `VolumeBindingMode` setting information from all storage classes in the universe by executing the following command:

  ```sh
  kubectl get storageclass
  ```
  Expect an output similar to the following:

  ```output
  NAME                 PROVISIONER             RECLAIMPOLICY   VOLUMEBINDINGMODE      ALLOWVOLUMEEXPANSION   AGE
  premium-rwo          pd.csi.storage.gke.io   Delete          WaitForFirstConsumer   true                   418d
  standard (default)   kubernetes.io/gce-pd    Delete          Immediate              true                   418d
  standard-rwo         pd.csi.storage.gke.io   Delete          WaitForFirstConsumer   true                   418d
  ```

- If a storage class name standard was defined in the universe, you can obtain information about a specific storage class by executing the following command:

  ```sh
  kubectl describe storageclass standard
  ```
  Expect an output similar to the following:
  
  ```output
    Name:                  standard
    IsDefaultClass:        Yes
    Annotations:           storageclass.kubernetes.io/is-default-class=true
    Provisioner:           kubernetes.io/gce-pd
    Parameters:            type=pd-standard
    AllowVolumeExpansion:  True
    MountOptions:          <none>
    ReclaimPolicy:         Delete
    VolumeBindingMode:     Immediate
    Events:                <none>
  ```

**Resolution**

Since not setting `VolumeBindingMode` to `WaitForFirstConsumer` might result in the universe creating the volume in a different zone than the selected zone, ensure that the `StorageClass` used during YugabyteDB Anywhere deployment has its `WaitForFirstConsumer` set to `VolumeBindingMode`. You can use the following command:

  ```sh
kubectl get storageclass standard -ojson \
    | jq '.volumeBindingMode="WaitForFirstConsumer" | del(.metadata.managedFields, .metadata.creationTimestamp, .metadata.resourceVersion, .metadata.uid)' \
    | kubectl replace --force -f -
  ```

### Elastic Block Store (EBS) controller is missing in the Elastic Kubernetes Service

Execute the following command to check events for the persistent volume claim (PVC):

```sh
kubectl describe pvc <PVC_NAME> -n <NAMESPACE>
```
If the issue you are experiencing is due to the missing EBS controller, expect an output similar to the following:

```output
waiting for a volume to be created, either by external provisioner "ebs.csi.aws.com" or manually created by system administrator
```

**Resolution**

Follow the instructions provided in [Troubleshoot AWS EBS volumes](https://aws.amazon.com/premiumsupport/knowledge-center/eks-troubleshoot-ebs-volume-mounts/).

## Pod failure to run

In some cases, a scheduled pod fails to run and errors are thrown.

### ImagePullBackOff and ErrImagePull errors

A Kubernetes pod may encounter these errors when it fails to pull the container images from a private container registry and the pod enters the [ImagePullBackOff](https://kubernetes.io/docs/concepts/containers/images/#imagepullbackoff) state.

The following are some of the specific reasons for the errors:
- Incorrect image path.
- Network failure or limitation.
- The `kubelet` node agent cannot authenticate with the container registry.

To start diagnostics, execute the following command to obtain the pod:

```sh
kubectl get pod -n <NAMESPACE>
```
If the issue you are experiencing is due to the image pull error, expect to initially see the `ErrImagePull` error, and on subsequent attempts the `ImagePullBackOff` error listed under `STATUS`, as per the following output:

```output
NAME                 READY   STATUS                  RESTARTS   AGE
yw-test-yugaware-0   0/4     Init:ErrImagePull       0          3

NAME                 READY   STATUS                  RESTARTS   AGE
yw-test-yugaware-0   0/4     Init:ImagePullBackOff   0          2m10s
```

Execute the following command to obtain detailed information about the pod and failure:

```sh
kubectl describe pod <POD_NAME> -n <NAMESPACE>
```
Expect an output similar to the following:

```output
Events:
  Type     Reason                  Age                 From                     Message
  ----     ------                  ----                ----                     -------
  Normal   Pulling                 25s (x3 over 75s)   kubelet                  Pulling image "quay.io/yugabyte/yugaware:2.16.0.0-b90"
  Warning  Failed                  22s (x3 over 72s)   kubelet                  Failed to pull image "quay.io/yugabyte/yugaware:2.16.0.0-b90": rpc error: code = Unknown desc = failed to pull and unpack image "quay.io/yugabyte/yugaware:2.16.0.0-b90": failed to resolve reference "quay.io/yugabyte/yugaware:2.16.0.0-b90": pulling from host quay.io failed with status code [manifests 2.16.0.0-b90]: 401 UNAUTHORIZED
  Warning  Failed                  22s (x3 over 72s)   kubelet                  Error: ErrImagePull
  Normal   BackOff                 7s (x3 over 72s)    kubelet                  Back-off pulling image "quay.io/yugabyte/yugaware:2.16.0.0-b90"
  Warning  Failed                  7s (x3 over 72s)    kubelet                  Error: ImagePullBackOff
```

**Resolution**

- To resolve the Bad pull secret, No pull secret, Bad pull secret name errors, enable the pull secret to fetch the images from the YugabyteDB Quay.io registry and ensure that you have applied the same in the namespace that will be used to install YugabyteDB Anywhere. By default, search for a secret with name `yugabyte-k8s-pull-secret` is performed. For more information, see [values.yaml](https://github.com/yugabyte/charts/blob/24a8dcf3a4c33153477e3e3ba82f9f4b6e2967a5/stable/yugaware/values.yaml#L16).
- To resolve the Unable to pull image error, ensure that the Kubernetes nodes can connect to Quay.io or you have images in the local registry. For more information, see [Pull and push yugabytedb docker images to private container registry](../../../install-yugabyte-platform/prepare-environment/kubernetes/#pull-and-push-yugabytedb-docker-images-to-private-container-registry).


### CrashLoopBackOff error

There is a number of reasons for the `CrashLoopBackOff` error. It typically occurs when a YugabyteDB Anywhere pod crashes due to an internal application error.

To start diagnostics, execute the following command to obtain the pod:

```sh
kubectl get pod -n <NAMESPACE>
```
If the issue you are experiencing is due to the `CrashLoopBackOff` error, expect to see this error listed under `STATUS`, as per the following output:

```output
NAME                             READY   STATUS             RESTARTS   AGE
yugabyte-platform-1-yugaware-0   3/4     CrashLoopBackOff   2          4d14h
```

**Resolution**

- Execute the following command to obtain detailed information about the YugabyteDB Anywhere pod experiencing the `CrashLoopBackOff` error:

  ```sh
  kubectl describe pods <POD_NAME> -n <NAMESPACE>
  ```

- Execute the following commands to check YugabyteDB Anywhere logs for a specific container and perform troubleshooting based on the information in the logs:

  ```sh
  # YugabyteDB Anywhere
  kubectl logs <POD_NAME> -n <NAMESPACE> -c yugaware

  # PostgreSQL
  kubectl logs <POD_NAME> -n <NAMESPACE> -c postgres
  ```

## Load balancer service is not ready

Load balancer might not be ready to provide services to a running YugabyteDB Anywhere instance.

### Incompatible load balancer

The internet-facing load balancer may not perform as expected because the default AWS load balancer used in Amazon Elastic Kubernetes Service (EKS) by the YugabyteDB Anywhere Helm chart is not suitable for your configuration.

**Resolution**

Use the following settings to customize the [AWS load balancer controller](https://kubernetes-sigs.github.io/aws-load-balancer-controller/v2.2/guide/service/annotations/) behavior:

  - Set `aws-load-balancer-scheme` to the `internal` or `internet-facing` string value.
  - Set `aws-load-balancer-backend-protocol` and `aws-load-balancer-healthcheck-protocol` to the `http` string value.

The following is a sample configuration:

  ```properties
  service.beta.kubernetes.io/aws-load-balancer-type: "ip"
  service.beta.kubernetes.io/aws-load-balancer-scheme: "internet-facing"
  service.beta.kubernetes.io/aws-load-balancer-backend-protocol: "http"
  service.beta.kubernetes.io/aws-load-balancer-healthcheck-protocol: "http"
  ```

### Pending state of load balancer IP assignment

Due to a variety of reasons, such as the absence of the load balancer controller or exceeded public IP quota, the load balancer IP assignment might enter a continuous pending state. 

To start diagnostics, execute the following command to obtain the switched virtual circuit (SVC) information:

```sh
kubectl get svc -n <NAMESPACE>
```
If the issue you are experiencing is due to IP assignment for the load balancer, expect to see an output similar to the following:

```output
NAME                          TYPE           CLUSTER-IP   EXTERNAL-IP   PORT(S)                       AGE
service/yw-test-yugaware-ui   LoadBalancer   10.4.1.7     <pending>     80:30553/TCP,9090:32507/TCP   15s
```

**Resolution**

Typically, cloud providers supply a load balancer controller that serves the load balancer type service. You need to verify whether or not the universe has the load balancer controller, as follows:

- If a load balancer controller is absent in Google Kubernetes Engine (GKE), follow instructions provided in [GKE ingress overview](https://cloud.google.com/kubernetes-engine/docs/concepts/ingress#overview).
- If the public IP is not associated with the load balancer, you might have exceeded the public IP quota limit.
- If you are using Minikube, run the [minikube tunnel](https://minikube.sigs.k8s.io/docs/commands/tunnel/).

## Other issues

A number of other issue can occur while installing and upgrading YugabyteDB Anywhere on Kubernetes.

### Cross-Origin Resource Sharing (CORS) error

You might encounter a CORS error while accessing YugabyteDB Anywhere through a load balancer. The condition can manifest itself by the initial setup or any login attempts not working or resulting in a blank screen.

To start diagnostics, check the developer tools of your browser for any errors. In addition, check the logs of the YugabyteDB Anywhere pod by executing the following command:
```sh
kubectl logs <POD_NAME> -n <NAMESPACE> -c yugaware
```
If the issue you are experiencing is due to the load balancer access-related CORS error, expect to see an error message similar to the following:

```output
2023-01-09T10:48:08.898Z [warn] 57fe083d-6ebb-49ab-bbaa-5e6576040d62
AbstractCORSPolicy.scala:311
[application-akka.actor.default-dispatcher-10275]
play.filters.cors.CORSFilter Invalid CORS
request;Origin=Some(https://localhost:8080);Method=POST;Access-Control-Request-Headers=None
```

**Resolution**

Specify correct domain names during the Helm installation or upgrade, as per instructions provided in [Set a DNS name](../../../install-yugabyte-platform/install-software/kubernetes/#set-a-dns-name).

### PVC expansion error

This error manifests itself in an inability to expand the PVC via the `helm upgrade` command. The error message should look similar to the following:

```output
Error: UPGRADE FAILED: cannot patch "yw-test-yugaware-storage" with kind PersistentVolumeClaim: persistentvolumeclaims "yw-test-yugaware-storage" is forbidden: only dynamically provisioned pvc can be resized and the storageclass that provisions the pvc must support resize
```

To start diagnostics, execute the following command to obtain information about the storage class:

```sh
kubectl describe sc <STORAGE_CLASS>
```
For example:
```sh
kubectl describe sc test-sc
```
The following output shows that the `AllowVolumeExpansion` parameter of the storage class is set to `false`:

```sh
Name:                  test-sc
IsDefaultClass:        No
Provisioner:           kubernetes.io/gce-pd
Parameters:            type=pd-standard
AllowVolumeExpansion:  False
MountOptions:          <none>
ReclaimPolicy:         Delete
VolumeBindingMode:     Immediate
Events:                <none>
```

**Resolution**

- Set the `AllowVolumeExpansion` parameter to `true` to expand the PVC, as follows:

  ```sh
  kubectl get storageclass <STORAGE_CLASS> -o json \
    | jq '.allowVolumeExpansion=true | del(.metadata.managedFields, .metadata.creationTimestamp, .metadata.resourceVersion, .metadata.uid)' \
    | kubectl replace --force -f -
  ```

- Use the following command to verify that `true` is returned:

  ```sh
  kubectl get storageclass <STORAGE_CLASS> -o json | jq '.allowVolumeExpansion'
  ```

- Increase the storage size using Helm upgrade and then execute the following command to obtain the persistent volume information:

  ```sh
  kubectl describe pvc <PVC_NAME> -n <NAMESPACE>
  ```
  Expect to see events for the PVC listed via an output similar to following:

  ```output
  Normal   ExternalExpanding           95s                volume_expand                                CSI migration enabled for kubernetes.io/gce-pd; waiting for external resizer to expand the pvc
  Warning  VolumeResizeFailed          85s                external-resizer pd.csi.storage.gke.io       resize volume "pvc-71315a47-d93a-4751-b48e-c7bfc365ae19" by resizer "pd.csi.storage.gke.io" failed: rpc error: code = DeadlineExceeded desc = context deadline exceeded
  Normal   Resizing                    84s (x2 over 95s)  external-resizer pd.csi.storage.gke.io       External resizer is resizing volume pvc-71315a47-d93a-4751-b48e-c7bfc365ae19
  Normal   FileSystemResizeRequired    84s                external-resizer pd.csi.storage.gke.io       Require file system resize of volume on node
  Normal   FileSystemResizeSuccessful  44s                kubelet                                      MountVolume.NodeExpandVolume succeeded for volume "pvc-71315a47-d93a-4751-b48e-c7bfc365ae19"
  ```
