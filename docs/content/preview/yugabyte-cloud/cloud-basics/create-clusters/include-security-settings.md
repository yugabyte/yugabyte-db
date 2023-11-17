<!--
+++
private = true
+++
-->

### Security

In addition to the volume encryption that YugabyteDB Managed uses to encrypt your data, you can enable YugabyteDB [encryption at rest](../../../cloud-secure-clusters/managed-ear/) (EAR) for clusters. When enabled, your YugabyteDB cluster (including backups) is encrypted using a customer managed key (CMK) residing in a cloud provider Key Management Service (KMS).

<!--You can also enable EAR for a cluster after the cluster is created.-->

To use a CMK to encrypt your cluster, make sure you have configured the CMK in AWS KMS, Azure Key Vault, or Google Cloud KMS. Refer to [Prerequisites](../../../cloud-secure-clusters/managed-ear/#prerequisites).

![Add Cluster Wizard - Security Settings](/images/yb-cloud/cloud-addcluster-security.png)

To use a CMK, select the **Enable cluster encryption at rest** option and set the following options:

- **KMS provider**: AWS, Azure, or GCP.
- For AWS:

  - **Customer managed key (CMK)**: Enter the Amazon Resource Name (ARN) of the CMK to use to encrypt the cluster.
  - **Access key**: Provide an access key of an [IAM identity](https://docs.aws.amazon.com/IAM/latest/UserGuide/id.html) with permissions for the CMK. An access key consists of an access key ID and the secret access key.

- For Azure:

  - The Azure [tenant ID](https://learn.microsoft.com/en-us/entra/fundamentals/how-to-find-tenant), the vault URI (for example, https://myvault.vault.azure.net), and the name of the key.
  - The client ID and secret for an application with permission to encrypt and decrypt using the CMK.

- For GCP:
  - **Resource ID**: Enter the resource ID of the key ring where the CMK is stored.
  - **Service Account Credentials**: Click **Add Key** to select the credentials JSON file you downloaded when creating credentials for the service account that has permissions to encrypt and decrypt using the CMK.
