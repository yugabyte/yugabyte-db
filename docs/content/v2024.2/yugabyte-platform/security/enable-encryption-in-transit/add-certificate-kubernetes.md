---
title: Add cert-manager certificates to YugabyteDB Anywhere
headerTitle: Add certificates
linkTitle: Add certificates
description: Add cert-manager certificates to YugabyteDB Anywhere.
headcontent: Use your own certificates for encryption in transit
menu:
  v2024.2_yugabyte-platform:
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

After the certificate is added to YugabyteDB Anywhere, set up the Kubernetes provider configuration by following the instructions in [Configure region and zones](../../../configure-yugabyte-platform/kubernetes/#configure-region-and-zones).

When adding a region, you can specify the Issuer kind, Issuer name, and optionally the Issuer group for each zone.

![Add new region](/images/yp/security/kubernetes-cert-manager-add-region-2024-2-2.png)

### Including the common name

If your certificate issuer (for example, for `aws-privateca-issuer`) requires the certificate to include the common name, set the following [override](../../../create-deployments/create-universe-multi-zone-kubernetes/#helm-overrides) for the provider region:

```yml
tls:
  certManager:
    certificates:
      commonNameRequired: true
```

When configured, YugabyteDB Anywhere sets the common name to the name of the service created for the pod, and adds common name to the certificate request sent to cert-manager.

### Configurable common names

You can configure a custom common name suffix for cert-manager certificates using the following helm override:

```yml
tls:
  certManager:
    certificates:
      commonNameRequired: true
      commonNameSuffix: "yugabyte.com"
```

## Rotate certificates in cert-manager

cert-manager monitors certificates and automatically renews them before expiration, based on the `renewBefore` setting in the [Certificate resource](https://cert-manager.io/v1.4-docs/usage/certificate/). Ensure that your certificate resources are properly configured with appropriate `renewBefore` values (for example, 15-30 days before expiry) to prevent certificate expiration.

To rotate certificates for a universe, after certificates are renewed in cert-manager, perform a rolling restart of your universe (**Actions > Initiate Rolling Restart**).

Note that, if you are using cert-manager for a universe, rotating node-to-node root certificates is not currently supported. To rotate these certificates, contact {{% support-platform %}}.

## Troubleshoot

If you encounter problems, you should verify the name of Issuer or ClusterIssuer in the Kubernetes cluster, as well as ensure that the Kubernetes cluster is in Ready state. You can use the following commands:

```sh
kubectl get ClusterIssuer
```

```sh
kubectl -n <namespace> Issuer
```
