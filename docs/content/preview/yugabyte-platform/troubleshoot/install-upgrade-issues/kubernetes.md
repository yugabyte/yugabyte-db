---
title: Install and upgrade issues on K8s
headerTitle: Install and upgrade issues on K8s
linkTitle: Install and upgrade issues
description: Troubleshoot issues encountered when installing or upgrading YugabyteDB Anywhere on K8s.
menu:
  preview_yugabyte-platform:
    identifier: install-upgrade-kubernetes-issues
    parent: troubleshoot-yp
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../vm/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      VM
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

You might encounter issues during installation and upgrade of YugabyteDB Anywhere.

If you experience difficulties while troubleshooting, contact [Yugabyte Support](https://support.yugabyte.com).

## Unable to schedule a pod once

There can be multiple reasons behind the failure of pod scheduling for YBA pods. The following can be the reasons behind it.

### Do we have ample resources?

**Symptom**

YBA pods are in the Pending state.

**Debug**

You can use the following commands to check the information about the pod. For more information, see [Kubernetes Troubleshooting guide](https://kubernetes.io/docs/tasks/configure-pod-container/assign-cpu-resource/#specify-a-cpu-request-that-is-too-big-for-your-nodes).

  ```sh
  kubectl get pods -n <NAMESPACE>

  ## output
  ## NAME                 READY   STATUS    RESTARTS   AGE
  ## yw-test-yugaware-0   0/4     Pending   0          2m30s

  kubectl describe pod <POD_NAME> -n <NAMESPACE>

  ## output
  ## Events:
  ##   Type     Reason             Age                From                Message
  ##   ----     ------             ----               ----                -------
  ##   Warning  FailedScheduling   56s                default-scheduler   0/2 nodes are available: 2 Insufficient cpu.
  ```

**Resolution**

1. Please ensure you have enough resources in the kubernetes cluster to schedule the YBA pods. For more information, see [Prerequisites](https://docs.yugabyte.com/preview/yugabyte-platform/install-yugabyte-platform/prerequisites/#hardware-requirements)

2. Modify the YBA pods resources configuration. Follow to understand the [YBA resources configuration](https://docs.yugabyte.com/stable/yugabyte-platform/install-yugabyte-platform/install-software/kubernetes/#customize-yugabytedb-anywhere) overrides.

### Node selectors/Taints & Tolerations/Affinity

We can schedule YBA pods on the particular nodes as required. We might see pods in the Pending state if the scheduler fails to figure out a node because pods need to match the labels or taints.
- For [Node Selector](https://docs.yugabyte.com/stable/yugabyte-platform/install-yugabyte-platform/install-software/kubernetes/#nodeselector)
- For [Affinity](https://docs.yugabyte.com/stable/yugabyte-platform/install-yugabyte-platform/install-software/kubernetes/#zoneaffinity)
- For [Taints & Tolerations](https://docs.yugabyte.com/stable/yugabyte-platform/install-yugabyte-platform/install-software/kubernetes/#toleration)

### Is StorageClass VolumeBindingMode set to WaitForFirstConsumer?

**Debug**

You can use following commands to check the storage class related information.

- Get all storage classes along with their `VolumeBindingMode` in the cluster.

  ```sh
  kubectl get storageclass

  ## output
  ## NAME                 PROVISIONER             RECLAIMPOLICY   VOLUMEBINDINGMODE      ALLOWVOLUMEEXPANSION   AGE
  ## premium-rwo          pd.csi.storage.gke.io   Delete          WaitForFirstConsumer   true                   418d
  ## standard (default)   kubernetes.io/gce-pd    Delete          Immediate              true                   418d
  ## standard-rwo         pd.csi.storage.gke.io   Delete          WaitForFirstConsumer   true                   418d
  ```

- Get information for particular storage class

  ```sh
  ## Considering standard named storage class exists in the cluster.
  kubectl describe storageclass standard

  ## output
  ## Name:                  standard
  ## IsDefaultClass:        Yes
  ## Annotations:           storageclass.kubernetes.io/is-default-class=true
  ## Provisioner:           kubernetes.io/gce-pd
  ## Parameters:            type=pd-standard
  ## AllowVolumeExpansion:  True
  ## MountOptions:          <none>
  ## ReclaimPolicy:         Delete
  ## VolumeBindingMode:     Immediate
  ## Events:                <none>
  ```

**Resolution**

Ensure the `StorageClass` used during YBA deployment should have `WaitForFirstConsumer` as `VolumeBindingMode`. Use the following command snippet to update the `VolumeBindingMode` of a `StorageClass`.

  ```sh
  ## Considering standard named storage class exists in the cluster.
  kubectl get storageclass standard -ojson | jq '.volumeBindingMode="WaitForFirstConsumer" | del(.metadata.managedFields, .metadata.creationTimestamp, .metadata.resourceVersion, .metadata.uid)' | kubectl replace --force -f -
  ```

## Fix ImagePullBackOff/ErrImagePull Error

**Symptom**

Kubernetes pods sometimes encounter this issue when it fails to pull the container images from a secured container registry. If an error occurs during the pull, the pod goes into the `ImagePullBackOff` state. For more information, see [ImagePullBackOff](https://kubernetes.io/docs/concepts/containers/images/#imagepullbackoff).

The `ImagePullBackOff` state occurs due to the following reasons -
1. The image path is incorrect.
2. The network fails - unable to pull image due to network limitation.
3. The `kubelet` does not succeed in authenticating with the container registry.

**Debug**

- Check the pod status

  ```sh
  kubectl get pod -n <NAMESPACE>

  ## output
  ## Initially, you will see ErrImagePull, but after trying multiple times, you will see ImagePullBackOff.
  ## NAME                 READY   STATUS                  RESTARTS   AGE
  ## yw-test-yugaware-0   0/4     Init:ErrImagePull       0          33s

  ## NAME                 READY   STATUS                  RESTARTS   AGE
  ## yw-test-yugaware-0   0/4     Init:ImagePullBackOff   0          2m10s
  ```

- Describe the pod to debug it further.

  ```sh
  kubectl describe pod yw-test-yugaware-0 -n <NAMESPACE>

  ## output
  ## Events:
  ##   Type     Reason                  Age                 From                     Message
  ##   ----     ------                  ----                ----                     -------
  ##   Normal   Scheduled               92s                 default-scheduler        Successfully assigned test-docs/yw-test-yugaware-0 to gke-yatish-cluster-1-pool-1-9d54814c-anja
  ##   Normal   SuccessfulAttachVolume  81s                 attachdetach-controller  AttachVolume.Attach succeeded for volume "pvc-8e7187b8-a755-43f5-964e-021ad8c41be5"
  ##   Normal   Pulling                 25s (x3 over 75s)   kubelet                  Pulling image "quay.io/yugabyte/yugaware:2.16.0.0-b90"
  ##   Warning  Failed                  22s (x3 over 72s)   kubelet                  Failed to pull image "quay.io/yugabyte/yugaware:2.16.0.0-b90": rpc error: code = Unknown desc = failed to pull and unpack image "quay.io/yugabyte/yugaware:2.16.0.0-b90": failed to resolve reference "quay.io/yugabyte/yugaware:2.16.0.0-b90": pulling from host quay.io failed with status code [manifests 2.16.0.0-b90]: 401 UNAUTHORIZED
  ##   Warning  Failed                  22s (x3 over 72s)   kubelet                  Error: ErrImagePull
  ##   Normal   BackOff                 7s (x3 over 72s)    kubelet                  Back-off pulling image "quay.io/yugabyte/yugaware:2.16.0.0-b90"
  ##   Warning  Failed                  7s (x3 over 72s)    kubelet                  Error: ImagePullBackOff
  ```



**Resolution**

- In case of **Bad pull secret / no pull secret** - You need a pull secret to fetch the images from Yugabyte Quay.io and ensure you have applied the same in the namespace, which will use to install YBA.
- For **Unable to pull image** - Ensure the kubernetes nodes have connectivity to Quay.io, or you should have images in the local registry. For more information, see [pull and push yugabytedb docker images to private container registry](https://docs.yugabyte.com/preview/yugabyte-platform/install-yugabyte-platform/prerequisites/#pull-and-push-yugabytedb-docker-images-to-private-container-registry).


## Pods are in CrashLoopBackOff

**Symptom**

There can be multiple reasons behind `CrashLoopBackOff`. YBA pod can crash due to some internal application error.

**Debug**

You can use `kubectl` to analyze the situation. We need to check YBA logs to understand the failure.

- Get all pods with their status in a namespace.

  ```sh
  kubectl get pod -n <NAMESPACE>

  ## output
  ## NAME                             READY   STATUS    RESTARTS   AGE
  ## yugabyte-platform-1-yugaware-0   4/4     Running   0          4d14h
  ```

- Describe the YBA pod facing `CrashLoopBackOff`. Check for `Events`.

  ```sh
  kubectl describe pods <YBA-POD-NAME> -n <NAMESPACE>
  ```

- Check the logs for particular container

  ```sh
  ## yugabytedb anywhere
  kubectl logs <YBA-POD-NAME> -n <NAMESPACE> -c yugaware

  ## postgres
  kubectl logs <YBA-POD-NAME> -n <NAMESPACE> -c postgres
  ```

## CORS error while accessing YBA through LB

You might experience the issue while accessing the YugabyteDB Anywhere through a load balancer. You can define the Cross-Origin Resource Sharing (CORS) domain configuration by setting the [additionAllowedCorsOrigins](https://github.com/yugabyte/charts/blob/master/stable/yugaware/values.yaml#L66) value to the new domain involved. For example, you would add the following to the appropriate Helm command.

  ```properties
   --set additionAllowedCorsOrigins:'https://mylbdomain'
  ```

## Load Balancer + EKS

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

## Is the load balancer up?

### Is a load balancer controller avail?

Most cloud providers provide the load balancer controller to serve the load balancer type service. You need to install the controller manually in case of its absence, or else the load balancer service wouldn't work as intended. For GKE, see [GKE Ingress](https://cloud.google.com/kubernetes-engine/docs/concepts/ingress#overview).

### Public IP quota

You might experience Public IP not associated with the load balancer if you exceed the Public IP quota limit.

## Unable to expand PVC

**Symptom**

You will see the error related to unable to expand the PVC.

**Debug**

The following parameter should be set to `true` to expand the PVC. You can verify the parameter using following command.

```sh
## The following command should return true
kubectl get storageclass standard -o json | jq '.allowVolumeExpansion'
```

**Resolution**

You can use following command to set the `allowVolumeExpansion` to `true`.

```sh
kubectl get storageclass <STORAGE_CLASS> -o json | jq '.allowVolumeExpansion=true | del(.metadata.managedFields, .metadata.creationTimestamp, .metadata.resourceVersion, .metadata.uid)' | kubectl replace --force -f -
```

## Helpful links
- [Debug pods](https://kubernetes.io/docs/tasks/debug/debug-application/debug-pods/)
- [Debug running pods](https://kubernetes.io/docs/tasks/debug/debug-application/debug-running-pod/)
- [Debug services](https://kubernetes.io/docs/tasks/debug/debug-application/debug-service/)
- [K8s troubleshooting guide for applications](https://kubernetes.io/docs/tasks/debug/debug-application/)
<!--

For YugabyteDB Anywhere HTTPS configuration, you should set your own key or certificate. If you do provide this setting, the default public key is used, creating a potential security risk.

-->
