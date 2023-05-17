<!--
+++
private = true
+++
-->

### Security

In addition to the volume encryption that YugabyteDB Managed uses to encrypt your data, you can enable YugabyteDB [encryption at rest](../../../cloud-secure-clusters/managed-ear/) (EAR) for clusters. When enabled, your YugabyteDB cluster (including backups) is encrypted using a customer managed key (CMK) residing in a cloud provider Key Management Service (KMS). (Currently, only [AWS KMS](https://docs.aws.amazon.com/kms/) is supported.)

You must be signed in as an Admin user to enable EAR. <!--You can also enable EAR for a cluster after the cluster is created.-->

To use a CMK to encrypt your cluster, make sure you have configured the CMK in AWS KMS, and created an [access key](https://docs.aws.amazon.com/IAM/latest/UserGuide/id_credentials_access-keys.html) for an IAM identity that has been granted permission to encrypt and decrypt using the CMK. For more information on AWS KMS, refer to [AWS Key Management Service](https://docs.aws.amazon.com/kms/) in the AWS documentation.

![Add Cluster Wizard - Security Settings](/images/yb-cloud/cloud-addcluster-security.png)

Set the following options:

- **Customer managed key (CMK)**: Enter the Amazon Resource Name (ARN) of the CMK to use to encrypt the cluster.
- **Access key**: Provide an access key of an [IAM identity](https://docs.aws.amazon.com/IAM/latest/UserGuide/id.html) with permissions for the CMK. An access key consists of an access key ID and the secret access key.
