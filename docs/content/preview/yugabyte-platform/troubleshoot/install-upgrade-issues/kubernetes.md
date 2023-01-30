---
title: Install and upgrade issues on Kubernetes
headerTitle: Install and upgrade issues on Kubernetes
linkTitle: Install and upgrade issues
description: Troubleshoot issues encountered when installing or upgrading YugabyteDB Anywhere on Kubernetes.
menu:
  preview_yugabyte-platform:
    identifier: install-upgrade-kubernetes-issues
    parent: troubleshoot-yp
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../kubernetes/" class="nav-link active">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../vm/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      VM
    </a>
  </li>

</ul>

You might encounter issues during installation and upgrade of YugabyteDB Anywhere.

If you experience difficulties while troubleshooting, contact [Yugabyte Support](https://support.yugabyte.com).

## Unable to schedule a pod

There can be multiple reasons behind the failure of pod scheduling for YBA pods. The following can be the reasons behind it. Follow to read about [Node selection in kube-scheduler](https://kubernetes.io/docs/concepts/scheduling-eviction/kube-scheduler/#kube-scheduler-implementation).

### Do we have sufficient resources?

**Symptom**

YBA pods are in the Pending state.

```sh
kubectl get pod -n <NAMESPACE>
```

```sh
NAME                 READY   STATUS    RESTARTS   AGE
yw-test-yugaware-0   0/4     Pending   0          2m30s
```

**Debug**

You can use the following commands to check the information about the pod. For more information, see [Kubernetes Troubleshooting guide](https://kubernetes.io/docs/tasks/configure-pod-container/assign-cpu-resource/#specify-a-cpu-request-that-is-too-big-for-your-nodes).

```sh
kubectl describe pod <POD_NAME> -n <NAMESPACE>
```

```sh
Events:
  Type     Reason             Age                From                Message
  ----     ------             ----               ----                -------
  Warning  FailedScheduling   56s                default-scheduler   0/2 nodes are available: 2 Insufficient cpu.
```

**Resolution**

1. Please ensure you have enough resources in the kubernetes cluster to schedule the YBA pods. For more information, see [Prerequisites](../../../install-yugabyte-platform/prerequisites/#kubernetes-based-installations)

2. Modify the YBA pods resources configuration. Follow the [YBA resources configuration](../../../install-yugabyte-platform/install-software/kubernetes/#modify-resources) to understand overrides.

### Node selectors/Taints & Tolerations/Affinity

**Symptom**

You will see something similar inside the events for the pod.

```sh
kubectl describe pod <POD_NAME> -n <NAMESPACE>
```

```sh
Warning  FailedScheduling   75s (x40 over 55m)    default-scheduler   0/55 nodes are available: 19 Insufficient cpu, 36 node(s) didn't match Pod's node affinity/selector
```

**Resolution**

We can schedule YBA pods on the particular nodes as required. We might see pods in the Pending state if the scheduler fails to figure out a node because pods need to match the labels or taints.
- For [Node Selector](../../../install-yugabyte-platform/install-software/kubernetes/#nodeselector)
- For [Affinity](../../../install-yugabyte-platform/install-software/kubernetes/#zoneaffinity)
- For [Taints & Tolerations](../../../install-yugabyte-platform/install-software/kubernetes/#tolerations)

### Is StorageClass VolumeBindingMode set to WaitForFirstConsumer?

**Symptom**

You will see some pods pending during the multi-zone deployment of YugabyteDB. If we do not set the `VolumeBindingMode` to `WaitForFirstConsumer`, the cluster might create volume in a different zone than the selected one.

```sh
kubectl describe pod <POD_NAME> -n <NAMESPACE>
```

```sh
Warning  FailedScheduling   75s (x40 over 55m)    default-scheduler   0/55 nodes are available: 19 Insufficient cpu, 36 node(s) didn't match Pod's node affinity/selector
```

**Debug**

You can use following commands to check the storage class related information.

- Get all storage classes along with their `VolumeBindingMode` in the cluster.

  ```sh
  kubectl get storageclass
  ```

  ```sh
  NAME                 PROVISIONER             RECLAIMPOLICY   VOLUMEBINDINGMODE      ALLOWVOLUMEEXPANSION   AGE
  premium-rwo          pd.csi.storage.gke.io   Delete          WaitForFirstConsumer   true                   418d
  standard (default)   kubernetes.io/gce-pd    Delete          Immediate              true                   418d
  standard-rwo         pd.csi.storage.gke.io   Delete          WaitForFirstConsumer   true                   418d
  ```

- Get information for particular storage class

  ```sh
  # Considering a storage class named standard exists in the cluster.
  kubectl describe storageclass standard
  ```

  ```sh
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

Ensure the `StorageClass` used during YBA deployment should have `WaitForFirstConsumer` as `VolumeBindingMode`. Use the following command snippet to update the `VolumeBindingMode` of a `StorageClass`.

  ```sh
  # Considering a storage class named standard exists in the cluster.
  kubectl get storageclass standard -ojson \
    | jq '.volumeBindingMode="WaitForFirstConsumer" | del(.metadata.managedFields, .metadata.creationTimestamp, .metadata.resourceVersion, .metadata.uid)' \
    | kubectl replace --force -f -
  ```

### EBS controller missing in EKS

**Symptom**

Pods are pending, and nothing in `Events` using `kubectl describe pod <POD_NAME>`. You will see the following error in events for the PVC.

```sh
kubectl describe  pvc <PVC_NAME> -n <NAMESPACE>
```

```sh
waiting for a volume to be created, either by external provisioner "ebs.csi.aws.com" or manually created by system administrator
```

**Debug**

- Run the following command to check the events for a PVC.

  ```sh
  kubectl describe pvc <PVC_NAME> -n <NAMESPACE>
  ```

**Resolution**

- Follow the well-written [Troubleshooting guide for EBS volumes](https://aws.amazon.com/premiumsupport/knowledge-center/eks-troubleshoot-ebs-volume-mounts/) from AWS.

## Scheduled but is not running

### Fix ImagePullBackOff/ErrImagePull Error

**Symptom**

Kubernetes pods sometimes encounter this issue when it fails to pull the container images from a private container registry. If an error occurs during the pull, the pod goes into the `ImagePullBackOff` state. For more information, see [ImagePullBackOff](https://kubernetes.io/docs/concepts/containers/images/#imagepullbackoff).

The `ImagePullBackOff` state can occur due to the following reasons -
1. The image path is incorrect.
2. The network fails - unable to pull image due to network limitation.
3. The `kubelet` does not succeed in authenticating with the container registry.

```sh
kubectl get pod -n <NAMESPACE>
```

```sh
# Initially, you will see ErrImagePull, but after trying multiple times, you will see ImagePullBackOff.
NAME                 READY   STATUS                  RESTARTS   AGE
yw-test-yugaware-0   0/4     Init:ErrImagePull       0          3

NAME                 READY   STATUS                  RESTARTS   AGE
yw-test-yugaware-0   0/4     Init:ImagePullBackOff   0          2m10s
```

**Debug**

- Describe the pod to debug it further.

  ```sh
  kubectl describe pod <POD_NAME> -n <NAMESPACE>
  ```

  ```sh
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

- In case of **Bad pull secret / no pull secret / Bad pull secret name** - You need a pull secret to fetch the images from Yugabyte Quay.io registry and ensure you have applied the same in the namespace, which will be used to install YBA. It will look for a secret with name `yugabyte-k8s-pull-secret` by default. For more information, see [values.yaml](https://github.com/yugabyte/charts/blob/24a8dcf3a4c33153477e3e3ba82f9f4b6e2967a5/stable/yugaware/values.yaml#L16).
- For **Unable to pull image** - Ensure the kubernetes nodes have connectivity to Quay.io, or you should have images in the local registry. For more information, see [pull and push yugabytedb docker images to private container registry](https://docs.yugabyte.com/preview/yugabyte-platform/install-yugabyte-platform/prerequisites/#pull-and-push-yugabytedb-docker-images-to-private-container-registry).


### Pods are in CrashLoopBackOff

**Symptom**

There can be multiple reasons behind `CrashLoopBackOff`. YBA pod can crash due to some internal application error.

```sh
kubectl get pod -n <NAMESPACE>
```

```sh
NAME                             READY   STATUS             RESTARTS   AGE
yugabyte-platform-1-yugaware-0   3/4     CrashLoopBackOff   2          4d14h
```

**Debug**

You can use `kubectl` to analyze the situation. We need to check YBA logs to understand the failure.

- Describe the YBA pod facing `CrashLoopBackOff`. Check for `Events`.

  ```sh
  kubectl describe pods <POD_NAME> -n <NAMESPACE>
  ```

- Check the logs for particular container

  ```sh
  # yugabytedb anywhere
  kubectl logs <POD_NAME> -n <NAMESPACE> -c yugaware

  # postgres
  kubectl logs <POD_NAME> -n <NAMESPACE> -c postgres
  ```

## Running but load balancer service is not ready

### Load Balancer + EKS

**Symptom**

You might face the internet-facing load balancer is not working as intended.

**Resolution**

Sometimes the default Amazon Web Services (AWS) load balancer brought up in Amazon Elastic Kubernetes Service (EKS) by the YugabyteDB Anywhere Helm chart is not suitable for your setup, you can use the following settings to customize the [AWS load balancer controller](https://kubernetes-sigs.github.io/aws-load-balancer-controller/v2.2/guide/service/annotations/) behavior.

  - `aws-load-balancer-scheme` can be set to `internal` or `internet-facing` string value.
  - `aws-load-balancer-backend-protocol` and `aws-load-balancer-healthcheck-protocol` should be set to the `http` string value.

  Consider the following sample configuration:

  ```properties
  service.beta.kubernetes.io/aws-load-balancer-type: "ip"
  service.beta.kubernetes.io/aws-load-balancer-scheme: "internet-facing"
  service.beta.kubernetes.io/aws-load-balancer-backend-protocol: "http"
  service.beta.kubernetes.io/aws-load-balancer-healthcheck-protocol: "http"
  ```

### Load balancer in a Pending state

**Symptom**

You will see the Load Balancer IP assignment in the Pending state. There can be multiple reasons for it.

- Absence of the load balancer controller
- Exceeds the Public IP quota

```sh
kubectl get svc -n <NAMESPACE>
```

```
NAME                          TYPE           CLUSTER-IP   EXTERNAL-IP   PORT(S)                       AGE
service/yw-test-yugaware-ui   LoadBalancer   10.4.1.7     <pending>     80:30553/TCP,9090:32507/TCP   15s
```

**Debug**

- Most cloud providers provide the load balancer controller to serve the load balancer type service. You need to verify whether the cluster has the load balancer controller.

**Resolution**

- In case of the absence of a load balancer controller in GKE, follow the [GKE Ingress Guide](https://cloud.google.com/kubernetes-engine/docs/concepts/ingress#overview).
- You might experience Public IP not associated with the load balancer if you exceed the Public IP quota limit.
- In case of Minikube, make sure to run the [minikube tunnel](https://minikube.sigs.k8s.io/docs/commands/tunnel/).

## Other issues

### CORS error while accessing YBA through LB

**Symptom**

You might experience the issue while accessing the YugabyteDB Anywhere through a load balancer. Initial setup or any login will not work, or it will give you a blank screen.

**Debug**

- Check the developer tools of your browser for any errors. Also, check the logs of YugabyteDB Anywhere pod, as follows:
  ```sh
  kubectl logs <POD_NAME> -n <NAMESPACE> -c yugaware
  ```

  ```
  2023-01-09T10:48:08.898Z [warn] 57fe083d-6ebb-49ab-bbaa-5e6576040d62
  AbstractCORSPolicy.scala:311
  [application-akka.actor.default-dispatcher-10275]
  play.filters.cors.CORSFilter Invalid CORS
  request;Origin=Some(https://localhost:8080);Method=POST;Access-Control-Request-Headers=None
  ```

**Resolution**

- Set the correct domain name(s) during Helm install or upgrade by following [Set a DNS name](../../../install-yugabyte-platform/install-software/kubernetes/#set-a-dns-name).

### Unable to expand PVC

**Symptom**

Unable to expand the PVC using `helm upgrade` and end up with the following error.

```sh
Error: UPGRADE FAILED: cannot patch "yw-test-yugaware-storage" with kind PersistentVolumeClaim: persistentvolumeclaims "yw-test-yugaware-storage" is forbidden: only dynamically provisioned pvc can be resized and the storageclass that provisions the pvc must support resize
```

**Debug**

- Describe the storage class to check the following `AllowVolumeExpansion` parameter.

  ```sh
  # Ex: kubectl describe sc test-sc
  kubectl describe sc <STORAGE_CLASS>
  ```

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

- The following `AllowVolumeExpansion` parameter should be set to `true` to expand the PVC. You can verify the parameter using following command.

  ```sh
  # The following command should return true
  kubectl get storageclass <STORAGE_CLASS> -o json | jq '.allowVolumeExpansion'
  ```

**Resolution**

- You can use following command to set the `allowVolumeExpansion` to `true`.

  ```sh
  kubectl get storageclass <STORAGE_CLASS> -o json \
    | jq '.allowVolumeExpansion=true | del(.metadata.managedFields, .metadata.creationTimestamp, .metadata.resourceVersion, .metadata.uid)' \
    | kubectl replace --force -f -
  ```

- You will see following events for the PVC after increasing the storage size using helm upgrade.

  ```sh
  kubectl describe  pvc <PVC_NAME> -n <NAMESPACE>
  ```

  ```sh
  Normal   ExternalExpanding           95s                volume_expand                                CSI migration enabled for kubernetes.io/gce-pd; waiting for external resizer to expand the pvc
  Warning  VolumeResizeFailed          85s                external-resizer pd.csi.storage.gke.io       resize volume "pvc-71315a47-d93a-4751-b48e-c7bfc365ae19" by resizer "pd.csi.storage.gke.io" failed: rpc error: code = DeadlineExceeded desc = context deadline exceeded
  Normal   Resizing                    84s (x2 over 95s)  external-resizer pd.csi.storage.gke.io       External resizer is resizing volume pvc-71315a47-d93a-4751-b48e-c7bfc365ae19
  Normal   FileSystemResizeRequired    84s                external-resizer pd.csi.storage.gke.io       Require file system resize of volume on node
  Normal   FileSystemResizeSuccessful  44s                kubelet                                      MountVolume.NodeExpandVolume succeeded for volume "pvc-71315a47-d93a-4751-b48e-c7bfc365ae19"
  ```

## Helpful links
- [Debug pods](https://kubernetes.io/docs/tasks/debug/debug-application/debug-pods/)
- [Debug running pods](https://kubernetes.io/docs/tasks/debug/debug-application/debug-running-pod/)
- [Debug services](https://kubernetes.io/docs/tasks/debug/debug-application/debug-service/)
- [Kubernetes troubleshooting guide for applications](https://kubernetes.io/docs/tasks/debug/debug-application/)
