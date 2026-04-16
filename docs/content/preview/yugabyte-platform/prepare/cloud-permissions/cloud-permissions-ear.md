---
title: Cloud setup for encryption at rest using YugabyteDB Anywhere
headerTitle: To use encryption at rest
linkTitle: To use encryption at rest
description: Prepare your cloud KMS for encryption at rest with YugabyteDB Anywhere.
headContent: Prepare your cloud KMS for use with YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: cloud-permissions-ear
    parent: cloud-permissions
    weight: 40
type: docs
---

YugabyteDB Anywhere (YBA) uses encryption at rest to secure data in YugabyteDB clusters. YBA uses key management services (KMS) to store the keys used to encrypt and decrypt the data. The key details are stored in YBA in [KMS configurations](../../../security/create-kms-config/aws-kms/). The cloud accounts associated with the KMS provider require permissions to manage the keys.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#aws" class="nav-link active" id="aws-tab" data-bs-toggle="tab"
      role="tab" aria-controls="aws" aria-selected="false">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="#gcp" class="nav-link" id="gcp-tab" data-bs-toggle="tab"
      role="tab" aria-controls="gcp" aria-selected="false">
      <i class="fa-brands fa-google"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="#azure" class="nav-link" id="azure-tab" data-bs-toggle="tab"
      role="tab" aria-controls="azure" aria-selected="false">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
</ul>

<div class="tab-content">

  <div id="aws" class="tab-pane fade show active" role="tabpanel" aria-labelledby="aws-tab">

Encryption at rest in YugabyteDB Anywhere supports the use of [Amazon Web Services (AWS) KMS](https://aws.amazon.com/kms/).

The master key resource policy should include the following key policy [permissions](https://docs.aws.amazon.com/kms/latest/developerguide/kms-api-permissions-reference.html):

```yaml
kms:Encrypt
kms:Decrypt
kms:GenerateDataKeyWithoutPlaintext
kms:DescribeKey
kms:DisableKey
kms:ScheduleKeyDeletion
kms:CreateAlias
kms:DeleteAlias
kms:UpdateAlias
```

{{< note title="Note" >}}

To support master key rotation, after upgrading YBA from a version prior to 2.17.3, add the `kms:Encrypt` permission to any existing keys that are used by any AWS KMS configurations, if not already present.

{{< /note >}}

The AWS user associated with a KMS configuration requires the following minimum Identity and Access Management (IAM) KMS-related permissions:

```yaml
kms:CreateKey
kms:ListAliases
kms:ListKeys
kms:CreateAlias
kms:DeleteAlias
kms:UpdateAlias
kms:TagResource
```

  </div>

  <div id="gcp" class="tab-pane fade" role="tabpanel" aria-labelledby="gcp-tab">

Encryption at rest in YugabyteDB Anywhere supports the use of [Google Cloud KMS](https://cloud.google.com/security-key-management).

The Google Cloud user associated with a KMS configuration requires a custom role assigned to the service account with the following KMS-related permissions:

```sh
cloudkms.keyRings.create
cloudkms.keyRings.get
cloudkms.cryptoKeys.create
cloudkms.cryptoKeys.get
cloudkms.cryptoKeyVersions.useToEncrypt
cloudkms.cryptoKeyVersions.useToDecrypt
cloudkms.locations.generateRandomBytes
```

If you are planning to use an existing cryptographic key with the same name, it must meet the following criteria:

- The primary cryptographic key version should be in the Enabled state.
- The purpose should be set to symmetric ENCRYPT_DECRYPT.
- The key rotation period should be set to Never (manual rotation).

Note that YugabyteDB Anywhere does not manage the key ring and deleting the KMS configuration does not destroy the key ring, cryptographic key, or its versions on Google Cloud KMS.

  </div>

  <div id="azure" class="tab-pane fade" role="tabpanel" aria-labelledby="azure-tab">

Encryption at rest in YugabyteDB Anywhere supports the use of Microsoft [Azure Key Vault](https://learn.microsoft.com/en-us/azure/key-vault/).

Before defining a KMS configuration with YugabyteDB Anywhere, you need to create a key vault through the [Azure portal](https://docs.microsoft.com/en-us/azure/key-vault/general/quick-create-portal). The following settings are required:

- Set the vault permission model as Vault access policy.
- Add the application to the key vault access policies with the minimum key management operations permissions of Get and Create (unless you are pre-creating the key), as well as cryptographic operations permissions of Unwrap Key and Wrap Key.

If you are planning to use an existing cryptographic key with the same name, it must meet the following criteria:

- The primary key version should be in the Enabled state.
- The activation date should either be disabled or set to a date before the KMS configuration creation.
- The expiration date should be disabled.
- Permitted operations should have at least WRAP_KEY and UNWRAP_KEY.
- The key rotation policy should not be defined in order to avoid automatic rotation.

Note that YugabyteDB Anywhere does not manage the key vault and deleting the KMS configuration does not delete the key vault, master key, or key versions on Azure Key Vault.

  </div>

</div>
