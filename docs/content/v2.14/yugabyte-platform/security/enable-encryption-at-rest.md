---
title: Enable encryption at rest
headerTitle: Enable encryption at rest
linkTitle: Enable encryption at rest
description: Enable encryption at rest
menu:
  v2.14_yugabyte-platform:
    parent: security
    identifier: enable-encryption-at-rest
    weight: 28
type: docs
---

Data at rest within a YugabyteDB universe should be protected from unauthorized users by encrypting it. The YugabyteDB Anywhere UI allows you to do the following:

 <!-- no toc -->

- [Enable encryption at rest during universe creation](#enable-encryption-at-rest-during-universe-creation)
- [Enable encryption at rest on an existing universe](#enable-encryption-at-rest-on-an-existing-universe)
- [Back up and restore data from an encrypted at rest universe](#back-up-and-restore-data-from-an-encrypted-at-rest-universe)
- [Rotate the universe keys used for encryption at rest](#rotate-the-universe-keys-used-for-encryption-at-rest)
- [Disable encryption at rest](#disable-encryption-at-rest)

There are two types of keys that are used to encrypt data in YugabyteDB:

- Universe key: The top-level symmetric key used to encrypt other keys and are common to the universe.
- Data key: The symmetric key used to encrypt data. There is one data key generated per flushed file.

For more information on the features, assumptions, design, data key management, universe keys, key rotations, master failures, and adding a node, see [Encryption at rest in YugabyteDB](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-encryption-at-rest.md).

## Enable encryption at rest during universe creation

You can enable encryption at rest during the universe creation as follows:

1. Open the YugabyteDB Anywhere UI and navigate to **Universes**.
2. Click **Create Universe** to open the **Universe Configuration** page.
3. After you select a provider, the **Instance Configuration** section expands to show more options.
4. Select the **Enable Encryption at Rest** option. The **Key Management Service Config** option appears.
5. Select your key management service (KMS) configuration from the **Key Management Service Config** list. The list displays only preconfigured KMS configurations. If you need to create one, see [Create a KMS configuration using AWS KMS](../create-kms-config/aws-kms/) or [Create a KMS configuration using HashiCorp Vault](../create-kms-config/hashicorp-kms/).
6. Continue with your universe creation, then click **Create**.

You can verify that encryption at rest has been successfully configured as follows:

1. Open the YugabyteDB Anywhere UI and navigate to the universe.
2. Select **Nodes**.
3. On one of the nodes, click **Master** under the **PROCESSES** column to open the overview.
4. To the right of **Replication Factor**, click **See full config** to open the **Current Cluster Config** page.
5. Verify that the configuration includes the following `encryption_info` section with the correct values:

    ```json
    encryption_info {
      encryption_enabled: true
      universe_key_registry_encoded: ".*"
      key_in_memory: true
      latest_version_id: ".*"
    }
    ```

If your configuration includes AWS KMS, the following occurs: once the universe has been created with encryption at rest enabled, YugabyteDB Anywhere persists the ciphertext of the universe key (because AWS does not persist any CMK-generated data keys themselves) and requests the plaintext of the universe key from AWS KMS using the KMS configuration whenever it needs to provide the universe key to the master nodes. For more information, see [Create a KMS configuration using AWS KMS](../create-kms-config/aws-kms/).

## Enable encryption at rest on an existing universe

You can enable encryption at rest on an existing universe as follows:

1. Open the YugabyteDB Anywhere UI and click **Universes**.

2. Select the universe for which to enable encryption.

3. Select **Actions > Edit Security > Encryption-at-Rest**.

4. In the **Manage Encryption at Rest** dialog, toggle **Enable Encryption at Rest for <universe_name>**.

   When the encryption is enabled, the **Key Management Service Config** option appears.

5. Select your KSM configuration from the **Key Management Service Config** list. The list displays only preconfigured KMS configurations. If you need to create one, see [Create a KMS configuration using AWS KMS](../create-kms-config/aws-kms/) or [Create a KMS configuration using HashiCorp Vault](../create-kms-config/hashicorp-kms/).

6. Click **Submit**.

7. Verify that the `EnableEncryptionAtRest` task completed successfully.

## Back up and restore data from an encrypted at rest universe

When you back up and restore universe data with encryption at rest enabled, YugabyteDB Anywhere requires a KMS configuration to manage backing up and restoring encrypted universe data. Because of the possibility that you will need to restore data to a different universe that might have a different universe key, YugabyteDB Anywhere ensures that all encrypted backups include a metadata file (that includes a list of key IDs in the source's universe key registry). When restoring your universe data, YugabyteDB Anywhere uses the selected KMS configuration to consume the universe key ID and then retrieve the universe key value for each key ID in the metadata file. Each of these keys are then sent to the destination universe to augment or build the universe key registry there.

## Rotate the universe keys used for encryption at rest

Enabling encryption and rotating a new key works in two steps:

1. Add the new universe key ID and key data to all the in-memory state of masters.
2. Issue a cluster configuration change to enable encryption with the new key.

The cluster configuration change does the following:

- Decrypts the universe key registry with the previous latest universe key.
- Adds the new key to the registry.
- Updates the cluster configuration with the new latest key ID.
- Encrypts the registry with the new key.

Once encryption is enabled with a new key, only new data is encrypted with this new key. Old data remains unencrypted, or encrypted with an older key, until compaction churn triggers a re-encryption with the new key.

To rotate the universe keys, perform the following:

1. Open the YugabyteDB Anywhere UI and navigate to the universe for which you want to rotate the keys.
2. Select **Actions > Edit Security > Encryption-at-Rest**.
3. Select **Rotate Key** and click **Submit**.

## Disable encryption at rest

You can disable encryption at rest for a universe as follows:

1. Open the YugabyteDB Anywhere UI and navigate to the universe for which to disable encryption.
2. Select **Actions > Edit Security > Encryption-at-Rest**.
3. In the **Manage Encryption at Rest** dialog, toggle **Enable Encryption at Rest for <universe_name>** and click **Submit**.
4. To verify that encryption at rest is disabled, check the current cluster configuration for each node to see that it contains `encryption_enabled: false`.
