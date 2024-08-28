---
title: Add cert-manager certificates to YugabyteDB Anywhere
headerTitle: Add certificates
linkTitle: Add certificates
description: Add cert-manager certificates to YugabyteDB Anywhere.
headcontent: Use your own certificates for encryption in transit
menu:
  preview_yugabyte-platform:
    parent: enable-encryption-in-transit
    identifier: add-certificate-4-kubernetes
    weight: 20
type: docs
---

{{<tabs>}}
{{<tabitem href="../add-certificate-self/" text="Self-Signed" >}}
{{<tabitem href="../add-certificate-ca/" text="CA-Signed" >}}
{{<tabitem href="../add-certificate-hashicorp/" text="Hashicorp Vault" >}}
{{<tabitem href="../add-certificate-kubernetes/" text="Kubernetes cert-manager" active="true" >}}
{{</tabs>}}

For a universe created on Kubernetes, YugabyteDB Anywhere allows you to configure an existing running instance of the [cert-manager](https://cert-manager.io/) as a TLS certificate provider for a cluster.

## Prerequisites

The following criteria must be met:

- The cert-manager is running in the Kubernetes cluster.
- A root or intermediate CA (either self-signed or external) is already configured on the cert-manager. The same CA certificate file, including any intermediate CAs, must be prepared for upload to YugabyteDB Anywhere. For intermediate certificates, the chained CA certificate can be constructed using a command similar to `cat intermediate-ca.crt root-ca.crt > bundle.crt`.
- An Issuer or ClusterIssuer Kind is configured on the cert-manager and is ready to issue certificates using the previously-mentioned root or intermediate certificate.
- Prepare the root certificate in a file (for example, `root.crt`).

## Add certificates using cert-manager

Add TLS certificates issued by the cert-manager as follows:

1. Navigate to **Integrations > Security > Encryption in Transit**.

1. Click **Add Certificate** to open the **Add Certificate** dialog.

1. Select **K8S cert-manager**.

    ![Add Kubernetes Certificate](/images/yp/encryption-in-transit/add-k8s-cert.png)

1. In the **Certificate Name** field, enter a meaningful name for your certificate.

1. Click **Upload Root Certificate** and select the CA certificate file that you prepared.

1. Click **Add** to make the certificate available.

## Configure the provider

After the certificate is added to YugabyteDB Anywhere, configure the Kubernetes provider configuration by following instructions provided in [Configure region and zones](../../../configure-yugabyte-platform/kubernetes/#configure-region-and-zones).

In the **Add new region** dialog shown in the following illustration, you would be able to specify the Issuer name or the ClusterIssuer name for each zone. Because an Issuer Kind is a Kubernetes namespace-scoped resource, the zone definition should also set the **Namespace** field value if an Issuer Kind is selected.

![Add new region](/images/yp/security/kubernetes-cert-manager-add-region.png)

## Troubleshoot

If you encounter problems, you should verify the name of Issuer or ClusterIssuer in the Kubernetes cluster, as well as ensure that the Kubernetes cluster is in Ready state. You can use the following commands:

```sh
kubectl get ClusterIssuer
```

```sh
kubectl -n <namespace> Issuer
```
