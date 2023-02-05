<!--
+++
private = true
+++
-->

### Security

In addition to the basic volume encryption that YugabyteDB Managed uses to encrypt your data, you can enable [database-level encryption](../../../cloud-secure-clusters/managed-ear/) for clusters. When enabled, your YugabyteDB database (including backups) is encrypted using a customer managed key (CMK) residing in a cloud provider Key Management Service (KMS). (Currently, only AWS KMS is supported, for clusters deployed in AWS.)

You must be signed in as an Admin user to enable cluster-level encryption. You can enable cluster encryption after the cluster is created.

![Add Cluster Wizard - Security Settings](/images/yb-cloud/cloud-addcluster-security.png)

To use your own customer managed key (CMK) to encrypt your data, make sure you have created an identity in your AWS IAM, and configured the CMK in AWS KMS. For more information on AWS KMS, refer to [AWS Key Management Service](https://docs.aws.amazon.com/kms/) in the AWS documentation.

Set the following options:

- **Customer managed key (CMK)**: Enter the Amazon Resource Name (ARN) of the CMK to use to encrypt the cluster database.
- **Access key**: Provide an access key of an IAM user with permissions for the CMK. An access key consists of an access key ID and a secret access key.

If YugabyteDB Managed validates the key successfully, validates the CMK.
