---
title: Install Yugabyte Platform Software - OpenShift
headerTitle: Install Yugabyte Platform Software - OpenShift
linkTitle: Install software 
description: Install Yugabyte Platform software in your OpenShift environment
menu:
  latest:
    parent: install-yugabyte-platform
    identifier: install-software-2-openshift
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
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/kubernetes" class="nav-link">
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
    <a href="/latest/yugabyte-platform/install-yugabyte-platform/install-software/openshift" class="nav-link active">
      <i class="fas fa-cloud"></i> OpenShift </a>
  </li>

</ul>

## Prerequisites

Before you install Yugabyte Platform Operator on an OpenShift cluster, prepare the environment, as described in [Prepare the OpenShift Environment](../../../install-yugabyte-platform/prepare-environment/openshift/).

## Installing the Yugabyte Platform Operator

You can install the Yugabyte Platform Operator via the OpenShift web console or command line.

### How to Use the OpenShift Web Console

You can install the Yugabyte Platform Operator as follows: 

- Login to the OpenShift Container Platform (OCP) cluster’s web console using admin credentials (for example, kube:admin).
- Navigate to the **Operators > OperatorHub**, search for Yugabyte Platform Operator, and then open it to display details about the operator, as shown in the following illustration:

![img](https://lh6.googleusercontent.com/Gyhf08ey1MinzqQduNL0sg-7cQOd41DeHasAoTvMomWFxxnCDMgVURL5sF4IwFwpgEcHbtp944OpTwgVl8IqxHR1JqHhe58S7PYUvUj_MEbq8fR49r82bRIamnsYTcmqyAZ0yF-2)

- Click **Install**.
- Accept default settings on the **Install Operator** page, as shown in the following illustration, and then click **Install**.

![img](https://lh4.googleusercontent.com/0vpvsAuUS-RgslYaiUoZsy1TqxEc7hA915d51HXG42gP1h2NJMFVFBPlfW5TxZHuIOtnVQMsXe7_6-Yyl8BDIcZmKcrX9ivNAzmpFyk1YsTOhHQBBrIc2cFFc6Ta3P5EimSKZIYd)

Once the installation is complete, a message shown in the following illustration is displayed:

![img](https://lh3.googleusercontent.com/yzcrTNR78MAAVyRr_5YN9tPA0nFF_xOGGGVdb1bZvsWNRZfS7NMEOQKp-HYcUtVB-67fIp5y7i-5glJXQn0GsjXl7mTOnQoZ7JBYe50Menba4ApdZtc1LRke4BvS7_VELR5X1WKH)

### How to Use the Command Line

Alternatively, you can install the operator via the command line. You start by configuring oc with an admin account (kube:admin) and following the procedure described in [Configuring oc with the OCP cluster](https://docs.google.com/document/d/1r8V1PkZGNOcnJMsx-vz0jQvRPpVTx8AulMn7Pa1LpoM/edit?ts=602fce22#heading=h.itkxhpp0uhvz).

To install the Yugabyte Platform Operator, execute the following command:

```shell
oc apply -f - <<EOF
apiVersion: operators.coreos.com/v1alpha1
kind: Subscription
metadata:
  name: yugabyte-platform-operator-bundle
  namespace: openshift-operators
spec:
  channel: alpha
  name: yugabyte-platform-operator-bundle
  source: certified-operators
  sourceNamespace: openshift-marketplace
EOF
# output
subscription.operators.coreos.com/yugabyte-platform-operator-bundle created
```

This creates a Subscription object and installs the operator in the cluster.

To verify that the operator pods are in Running state, execute the following command:

```shell
oc get pods -n openshift-operators | grep -E '^NAME|yugabyte-platform'
# output
NAME                                                         READY  STATUS  RESTARTS  AGE
yugabyte-platform-operator-controller-manager-7485db7486-6nzxr 2/2  Running  0      5m38s
```

For additional information, see [Adding Operators to a cluster](https://docs.openshift.com/container-platform/4.6/operators/admin/olm-adding-operators-to-cluster.html).

## Finding the Availability Zone Labels

You need to find the region name and availability zone codes where the cluster is running. This information is required by Yugabyte Platform (see [Creating a provider in Yugabyte Platform](https://docs.google.com/document/d/1r8V1PkZGNOcnJMsx-vz0jQvRPpVTx8AulMn7Pa1LpoM/edit?ts=602fce22#heading=h.hqqo8w43w706)). For example, if your OCP cluster is in the US East, then the cloud provider's zone labels can be us-east4-a, us-east4-b, and so on.

You can use the OpenShift web console or the command line to search for the availability zone codes.

### How to Use the OpenShift Web Console

You start by logging in the OCP's web console as admin user, and then performing the following:

- Navigate to **Compute > Machine Sets** and open each Machine Set.

- On the **Machine Set Details** page, open the **Machines** tab to access the region and availability zone label, as shown in the following illustration where the region is US East and the availability zone label is us-east4-a.

  ![img](https://lh3.googleusercontent.com/lIT9CxHBqoY1To2LPOYBGPpjPh7I5B4vQuZOiIK-XWy4ufsQpVEe-hTBzoADDxtTZfKwO0L7I2WlB5Fm3d5Dzreoj0z4_91WhCY2isTDxDzv5baAe0fbRK8J58vs8v1BCAygt84P)

- Log out of the admin account and login with your user account.

### How to Use the Command Line

Alternatively, you can find the availability zone codes via the command line.

You start by configuring oc with an admin account (kube:admin) and following the procedure described in [Configuring oc with the OCP cluster](https://docs.google.com/document/d/1r8V1PkZGNOcnJMsx-vz0jQvRPpVTx8AulMn7Pa1LpoM/edit?ts=602fce22#heading=h.itkxhpp0uhvz).

To find the region and zone labels, execute the following command:

```shell
oc get machinesets \  
  -n openshift-machine-api \  
  -ojsonpath='{range .items[*]}{.metadata.name}{", region: "}{.spec.template.spec.providerSpec.value.region}{", zone: "}{.spec.template.spec.providerSpec.value.zone}{"\n"}{end}'
# output
  ocp-dev4-l5ffp-worker-a, region: us-east4, zone: us-east4-a
  ocp-dev4-l5ffp-worker-b, region: us-east4, zone: us-east4-b
  ocp-dev4-l5ffp-worker-c, region: us-east4, zone: us-east4-c
```

After the execution, the region is displayed as US East and the zones as us-east4-a, us-east4-, and so on.

## Configuring oc with the OCP Cluster

To configure the command-line interface (CLI) tool oc, login to the OCP web console with your user account.

At the top right, click your user name and select **Copy Login Command**, as shown in the following illustration:

![img](https://lh6.googleusercontent.com/2FJo0Tt7rOqFInE8cCT8rG5OS8HgLqbz99iJQVDrpBwloWvQVqPO_7L84NWdoheQF1lzRnoXJlwpwlIOgjvUfrKvyggsYkl9uvd9fUWkPQq1DRV4EzjxpihFo5FmcgHHVQr9Krwp)

If you are prompted to login again, login with your user account, and then click **Display Token**.

Copy and execute the command provided in **Log in with this token**. Depending on your cluster configuration, it might ask you to confirm that you intend to use a self-signed certificate by posting a **Use insecure connection?** message.

Unless otherwise specified, you need to use a user account (as opposed to an admin user account) for executing all the commands described in the remainder of this document.

## Creating an Instance of Yugabyte Platform

You start by creating an instance of Yugabyte Platform in a new project (namespace) called yb-platform. To do this, you can use the OpenShift web console or command line.

### How to Use the OpenShift Web Console

You can create an instance of Yugabyte Platform via he OpenShift web console as follows:

- Open the OCP web console and navigate to **Home > Projects > Create Project**.
- Enter the name yb-platform and click **Create**.
- Navigate to **Operators > Installed Operators** select **Yugabyte Platform Operator**, as shown in the following illustration:

![img](https://lh3.googleusercontent.com/kxlTWh54AnhHsD-5blzrcen-O0IFlGekL_hg46eYWTmQ5lVH1SKqZEhpwiwjy9Rb9qYfoidFML648m0_bFIeGBV6yqniGb5GJGRXaZHzS7GA7lwaMRKRcnGDFLAgooh1PtRYXgfv)

- Click **Create Instance** to open the **Create YBPlatform** page. 
- Review the default settings without modifying them and click **Create**. Ensure that the **yb-platform** project is selected.

Shortly, you should expect the **Status** column in the **Yugabyte Platform** tab to display **Deployed**, as shown in the following illustration:

![img](https://lh3.googleusercontent.com/1AN01b0xNQSqTEj4AO9BgZqBwoT8h22rW9pnunUjChc9y_pnqJ02uQWUmzaFFQNDQtayFmyAfI7Xz88GqOkuHYW0O1p8Uuc-5nDrFv_cvfSz2UMGR4sP_UeiYbo5qbfnlc9pszLS)

### How to Use the Command Line

Alternatively, you can create an instance of Yugabyte Platform via the command line.

To create a new project, execute the following command:

```shell
oc new-project yb-platform
# output
Now using project "yb-platform" on server "web-console-address"
```

To create an instance of Yugabyte Platform in the yb-platform project, execute the following command:

```shell
oc apply \  
  -n yb-platform \  
  -f - <<EOF
apiVersion: yugabyte.com/v1alpha1
kind: YBPlatform
metadata: 
  name: ybplatform-sample
spec: 
  image:  
    repository: registry.connect.redhat.com/yugabytedb/yugabyte-platform  
    tag: latest 
  ocpCompatibility:  
    enabled: true 
  rbac:  
    create: false
EOF
# output
ybplatform.yugabyte.com/ybplatform-sample created
```

To verify that the pods of the Yugabyte Platform instance are in Running state, execute the following:

```shell
oc get pods -n yb-platform -l app=ybplatform-sample-yugaware
# output
NAME                         READY  STATUS  RESTARTS  AGE
Ybplatform-sample-yugaware-0  5/5   Running  0        22s
```

## Accessing and Configuring Yugabyte Platform

Once you have created and deployed Yugabyte Platform, you can access its web UI and create an account.

### How to Find the IP Address to Access the Web UI

To find the IP address, you can use the OpenShift web console or the command line.

#### Using the OpenShift Web Console

You can locate the IP address using the OpenShift web console as follows:

- Use the OCP web console to navigate to **Networking > Services** and select **ybplatform-sample-yugaware-ui** from the list. Ensure that the **yb-platform** project is selected.
- In the **Service Routing** section of the **Details** tab, locate **External Load Balancer** and copy the IP address, as shown in the following illustration:

![img](https://lh4.googleusercontent.com/qFp6eln2e8vlB9YqmfUFbbxjZzcZHRyfQMoly-RopBNqmB4_2FPKssS3Wh8Y4slM65isqVOBkrsf_X-JF4KZEAcjfdczemTAFMlvW7DonY6hU27FHGyqdkZrnVe0eNGM2h398Bdb)

- Open the copied IP in a new instance of your web browser.

#### Using the Command Line

Alternatively, you can obtain the information about the IP address via the command line by executing the following command:

```shell
oc get services \ 
  ybplatform-sample-yugaware-ui \ 
  -ojsonpath='{.status.loadBalancer.ingress[0].ip}{"\n"}'
# output
12.34.56.78
```

### How to Register Via the Admin Console

The IP address allows you to navigate to the **Admin Console Registration** page of Yugabyte Platform shown in the following illustration:

![img](https://lh5.googleusercontent.com/60sn2tVOTdsXtrm5O2bZerf8VP3f4s5jWnpTfVKBZ1zNn7VeKZ2yuMrU3XZkbvZ5R2NNqwmxDSJaYI0dEd3fMXM2N80VOD8Vah0zMDqdNnOZ1zab2Pi0pQzYiKH9X7ojeX3Q8ItW)

- Complete the fields of the Admin Console Registration page and click **Register**.
- Login with the email and the password you provided during registration and observe the welcome screen shown in the following illustration:

![img](https://lh3.googleusercontent.com/ud4p4tcntfJc0YAflIjn5wmSoH31oQCPQg-Vpc0Z8Fin5GLERdqH8yDCD6-E7OxR4t0YvrRwW1ZAEHdzAUwMlVUFA3ZM8XPTTHMU7NVZaJsfc_OFzbqg-fPPu29fQURZLNCiINnN)

## Upgrading a Yugabyte Platform Instance

You can upgrade the Yugabyte Platform instance installed using the Operator to a new tag that you receive from Yugabyte. In the current release, you can do this by using the command line. 

The following example shows the command you would execute to update the container image tag to 2.5.2.0-b89:

```shell
oc patch \
 ybplatform ybplatform-sample \
 -p '{"spec":{"image":{"tag":"2.5.2.0-b89"}}}' --type merge \
 -n yb-platform
# output
ybplatform.yugabyte.com/ybplatform-sample patched
```

To verify that the pods are being updated, execute the following command:

```shell
oc get pods -n yb-platform -l app=ybplatform-sample-yugaware -w
# output
NAME                         READY  STATUS          RESTARTS  AGE
ybplatform-sample-yugaware-0  5/5   Running            0     18m
ybplatform-sample-yugaware-0  0/5   Terminating        0     19m
ybplatform-sample-yugaware-0  0/5   Pending            0     0s
ybplatform-sample-yugaware-0  0/5   ContainerCreating  0     35s
ybplatform-sample-yugaware-0  5/5   Running            0     93s
```

