---
title: Configure the OpenShift Cloud Provider
headerTitle: Configure the OpenShift Cloud Provider
linkTitle: Configure the cloud provider
description: Configure the OpenShift cloud provider
aliases:
  - /latest/deploy/enterprise-edition/configure-cloud-providers/openshift
menu:
  latest:
    identifier: set-up-cloud-provider-5-openshift
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
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes" class="nav-link">
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
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/openshift" class="nav-link active">
      <i class="fas fa-cubes" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises" class="nav-link">
      <i class="fas fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

This document describes how to configure OpenShift for YugabyteDB universes using Yugabyte Platform. If no cloud providers are configured via the Yugabyte Platform console, the main Dashboard page requests to configure at least one provider.

![Configure Cloud Provider](/images/ee/configure-cloud-provider.png)

To create a YugabyteDB universe using the deployed platform, you start by creating the required Role-based access control (RBAC) and adding the provider in the platform.

## Creating RBAC and kubeconfig

kubeconfig is used by Yugabyte Platform to create universes in the OCP cluster.

To create a ServiceAccount in the yb-platform project, execute the following command:

```shell
oc apply \
  -n yb-platform \
  -f https://raw.githubusercontent.com/yugabyte/charts/master/rbac/yugabyte-platform-universe-management-sa.yaml
# output
serviceaccount/yugabyte-platform-universe-management created
```

The next step is to grant access to this ServiceAccount using Roles and RoleBindings, thus allowing it to manage the YugabyteDB universe's resources for you. To create the required RBAC objects, execute the following command:

```shell
curl -s https://raw.githubusercontent.com/yugabyte/charts/master/rbac/platform-namespaced.yaml \
 | sed "s/namespace: <SA_NAMESPACE>/namespace: yb-platform/g" \
 | oc apply -n yb-platform -f -
# output
role.rbac.authorization.k8s.io/yugabyte-helm-operations created
role.rbac.authorization.k8s.io/yugabyte-management created
rolebinding.rbac.authorization.k8s.io/yugabyte-helm-operations created
rolebinding.rbac.authorization.k8s.io/yugabyte-management created
```

The next step is to create a kubeconfig for this ServiceAccount. You download a helper script for generating a kubeconfig file by executing the following command:

```shell
python generate_kubeconfig.py \
 --service_account yugabyte-platform-universe-management \
 --namespace yb-platform
# output
Generated the kubeconfig file: /tmp/yugabyte-platform-universe-management.conf
```

## Creating a Provider in Yugabyte Platform

Since Yugabyte Platform manages YugabyteDB universes, Yugabyte Platform needs details about the cloud providers. In your case, the provider is your own OCP cluster.

You can create a provider as follows:

- Open the Yugabyte Platform web UI and click **Configure a Provider** to open the **Cloud Provider Configuration** page shown in the following illustration.
- Select **Red Hat OpenShift** and complete the fields, as follows:
  - In the **Type** filed, select **OpenShift**.
  - In the **Name** field, enter ocp-test.
  - Use the **Kube Config** field to select the file that you created in the preceding step.
  - In the **Service Account** field, enter yugabyte-platform-universe-management.
  - In the **Image Registry** field, use `registry.connect.redhat.com/yugabytedb/yugabyte`.
- Use the **Pull Secret File** field to upload the pull secret you received from Yugabyte Support (note that it is not required if you are using the Red Hat registry). 

![OpenShift Provider Config](/images/ee/openshift-cloud-provider-setup.png)

- Click **Add Region** and complete the **Add new region** dialog shown in the following illustration by first selecting the region you found previously (US East), and then entering the following information:
  - In the **Zone** field, enter the exact zone label (us-east4-a).
  - In the **Namespace** field, enter yb-platform.

![img](https://lh5.googleusercontent.com/gQ-fTRZnDTp4kCqNaKo8KUgVA2mJeCCjaHiqzdCOcG4350yxgDGZojMWhsfdvcpLJbOio8sL8K932wmDM6_S8fIkL9wfKAo-b3340Yvc1Dy4FJ61o2Ec93DptkB_Ski1XqaU3UrT)

- Click **Add Region**.
- Click **Save**. 

You should see the newly-added provider under **Red Hat OpenShift configs**.

## Creating a Universe Using the Provider

You can create a universe using the provider as follows:

- Use the Yugabyte Platform web UI to navigate to **Universes** and then click **Create Universe**. 

- Complete the **Create Universe** page shown in the following illustration by entering the following information:

  - In the **Name** field, enter universe-1.
  - In the **Provider** field, enter ocp-test.
  - In the **Regions** field, enter US East.
  - In the **Instance Type** field, enter xsmall (2 cores, 4GB RAM).

![img](https://lh3.googleusercontent.com/E8EwHd0olx_ZmmpRN-vpem8TAvqqU_gPQGyuMJH9yDxKS89cswTURFASkAE0fDKWMCfImUknxbyN7z87SzaOJJE_PO6Wo4aKw7UHIdXy7oCxPgTmv2XvRMvAIvbRQygS0gz3oXNz)

- Click **Create**. 

The following illustration shows the universe creation progress:

![img](https://lh3.googleusercontent.com/wmVq3lUnqseriSgXcw-gwqNRct4CxUSZ0NnGwgVJ5UG-7GU8Ja4kKHGUZ428BmYOZvcemMLlod_9PBWQAFaj99bOybyW_XEc40X6mZL9fZKi6IQ9q7-Cr52XTfzjmw0ppGTN8b9Z)

If you click universe-1 and then navigate to **Tasks**, you should see a page similar to the one shown in the following illustration:

![img](https://lh3.googleusercontent.com/UFeWUHRm7Amxek1U8yxqeV3z4RiUll05Vo9VowiCImlELaCabHyaDFFT81XYbbBXUMoDAK8g13q9FP4ZbwOSAoCzsqGV83zqF9Jkcw_3pb4U4WxgC7MOiULbCpAArxIqSHMrDDIC)

Upon successful creation of the universe, the **Overview** tab of universe-1 should look similar to the following:

![img](https://lh5.googleusercontent.com/TpdExY6idrjH64UXfXKsY9RRwPaLU5OWfNHD_VZxrzRDWAXmG_dftROfNyxRX7TNWr7BgWxrsuZXOa4N_4KP9RHt4c9AgleCGSmziKudtIbqzrJAqOu7YL0oa9MpfdCmBvNE7rPL)

## Troubleshooting the Universe Creation

If the universe creation remains in Pending state for more than 2-3 minutes, open the OCP web console, navigate to **Workloads > Pods** and check if any of the pods are in pending state, as shown in the following illustration:

![img](https://lh5.googleusercontent.com/jHJnEpQ6baGDDuppWND7O6z2SZM4dOPkIFekGa556xUffhOjEFdkM3EUvL2BEQXponEkEjYOwIFmPKu3z35-d903Vhz0D2i_6MyZEXstkrHN1jpgQQFgFy675cNwyzhonpGf65l9)

Alternatively, you can execute the following command to check status of the pods:

```shell
oc get pods -n yb-platform -l chart=yugabyte
# output
NAME          READY STATUS  RESTARTS AGE
yb-master-0   2/2   Running  0     5m58s
yb-master-1   2/2   Running  0     5m58s
yb-master-2   0/2   Pending  0     5m58s
yb-tserver-0  2/2   Running  0     5m58s
yb-tserver-1  2/2   Running  0     5m58s
yb-tserver-2  2/2   Running  0     5m58s
```

If any of the pods are in pending state, perform the following: 

- Login with an admin account and navigate to **Compute > Machine Sets**.
- Open the Machine Set corresponding to your zone label (us-east4-a). 
- Click **Desired Count** and increase the count by 1 or 2, as shown in the following illustration.
- Click **Save**.

![img](https://lh3.googleusercontent.com/N7ZHbdmANOWytA4Byyxd5P93Kq51t9QSI9Nr0xSK1YPlBTJOHpbKuz6NDwYlmV2v02ZE_k8F2Xx85KFWQx8mAldYb9TUT01M7Gf3jJMVmlbQdK6_9apgyCr7s8_XF3iWEvVFl0OO)

Alternatively, you can scale the MachineSets by executing the following command as admin user:

```shell
oc scale machineset ocp-dev4-l5ffp-worker-a --replicas=2 -n openshift-machine-api
# output
machineset.machine.openshift.io/ocp-dev4-l5ffp-worker-a scaled
```

As soon as the new machine is added, the pending pods become ready.