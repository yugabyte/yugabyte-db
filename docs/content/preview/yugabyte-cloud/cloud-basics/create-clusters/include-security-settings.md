<!--
+++
private = true
+++
-->

### Security

In addition to the volume encryption that YugabyteDB Managed uses to encrypt your data, you can enable [database encryption at rest](../../../cloud-secure-clusters/managed-ear/) for clusters. When enabled, your YugabyteDB database (including backups) is encrypted using a customer managed key (CMK) residing in a cloud provider Key Management Service (KMS). (Currently, only AWS KMS is supported, for clusters deployed in AWS.)

You must be signed in as an Admin user to enable cluster-level encryption. You can enable cluster encryption after the cluster is created.

![Add Cluster Wizard - Security Settings](/images/yb-cloud/cloud-addcluster-security.png)

To use your own customer managed key (CMK) to encrypt your data, make sure you have created an identity in your AWS IAM, and configured the CMK in AWS KMS. For more information on AWS KMS, refer to [AWS Key Management Service](https://docs.aws.amazon.com/kms/) in the AWS documentation.

Set the following options:

- **Customer managed key (CMK)**: Enter the Amazon Resource Name (ARN) of the CMK to use to encrypt the cluster database.
- **Access key**: Provide an access key of an [IAM identity](https://docs.aws.amazon.com/IAM/latest/UserGuide/id.html) with permissions for the CMK. An access key consists of an access key ID and the secret access key.
