---
title: Enable encryption at rest
headerTitle: Enable encryption at rest
linkTitle: Enable encryption at rest
description: Enable encryption at rest
menu:
  latest:
    parent: secure-universes
    identifier: enable-encryption-at-rest
    weight: 20
isTocNested: true
showAsideToc: true
---

## Encryption at rest overview

Data at rest within a YugabyteDB universe should be protected from unauthorized users by encrypting it. Using the Yugabyte Platform console,
you can:

- [Encryption at rest overview](#encryption-at-rest-overview)
- [Enable encryption at rest during universe creation](#enable-encryption-at-rest-during-universe-creation)
- [Enable encryption at rest on an existing universe](#enable-encryption-at-rest-on-an-existing-universe)
- [Back up and restore data from encrypted at rest universe](#back-up-and-restore-data-from-encrypted-at-rest-universe)
- [Rotate the encryption at rest keys](#rotate-the-encryption-at-rest-keys)
- [Disable encryption at rest](#disable-encryption-at-rest)

Two types of keys are used to encrypt data in YugabyteDB:

- Universe key: The top-level symmetric key used to encrypt other keys and are common to the universe.
- Data key: The symmetric key used to encrypt data. There is one data key generated per flushed file.

For details on the features, assumptions, design, data key management, universe keys, key rotations, master failures, and adding a node, see [Encryption At Rest in YugabyteDB](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/docdb-encryption-at-rest.md).

## Enable encryption at rest during universe creation

To enable encryption at rest during universe creation, follow these steps:

1. Open the Yugabyte Platform console and click **Universes**.
2. Click **Create Universe**. The **Universe Configuration** page appears.
3. After you select a provider, the **Instance Configuration** section expands to show more options.
4. Select the **Enable Encryption at Rest** option. The **Key Management Service Config** option appears.
5. Select your KMS configuration from the **Key Management Service Config** drop-down list. The list displays only preconfigured KMS configurations. If you need to create one, see [Create a KMS configuration](../create-kms-configuration).
6. Continue with your universe creation, then click **Create**.

To verify that encryption at rest has been successfully configured, follow these steps:

1. Open the Yugabyte Platform console and go to the universe.
2. Select the **Nodes** tab.
3. On one of the nodes, click **Master** under the **PROCESSES** column. The overview page appears.
4. To the right of **Replication Factor**, click **See full config**. The **Current Cluster Config** page appears.
5. Verify that the configuration includes the following `encryption_info` section with the correct values:

    ```json
    encryption_info {
      			encryption_enabled: true,
    Universe_key_registry_encoded: ".*",
    Key_in_memory: true,
    latest_version_id: ".*",
    }
    ```

6. Go to the **Security Object** page for the Equinix SmartKey account that was used to create the KMS configuration. A security object should exist with a name matching the universe UUID that was configured to use encryption at rest with the SMARTKEY configuration (defaults to AES 256).

**Equinix SmartKey**

Once the universe has started being created, a security object will be created with SmartKey through REST API with the universe UUID as the name of the security object. This object will then be exported to Yugaware as the universe key. Once received, Yugaware will then send the key to all masters to persist. Yugaware does not persist any information about the security object beyond a reference to the object’s kId. At this point, data within the universe will be encrypted at rest using data keys encrypted with the aforementioned universe key generated through the chosen KMS configuration.

**AWS KMS**

Once the universe has been created with encryption at rest enabled, the platform will persist the ciphertext of the universe key (because AWS does not persist any CMK-generated data keys themselves) and will request the plaintext of the universe key from AWS KMS using the KMS configuration whenever it needs to provide the universe key to the master nodes.

## Enable encryption at rest on an existing universe

To enable encryption at rest on an existing universe, follow these steps:

1. Open the Yugabyte Platform console and click **Universes**.
2. Select the universe you want to enable.
3. Click the **More** drop-down list and select **Manage Encryption Keys**. The **Manage Keys** dialog appears.
4. Select **Enable Encryption-at-Rest for <UNIVERSE_NAME>**. When enabled, the **Key Management Service Config** option appears.
5. Select your KSM configuration from the **Key Management Service Config** drop-down list. The list displays only preconfigured KMS configurations. If you need to create one, see [Create a KMS configuration](../create-kms-configuration).
6. Verify that the `EnableEncryptionAtRest` task completed successfully.

## Back up and restore data from encrypted at rest universe

## Rotate the encryption at rest keys

Enabling encryption and rotating a new key works in two steps:

1. Add the new universe key ID and key data to all the in-memory state of masters.
2. Issue a cluster configuration change to enable encryption with the new key.

The cluster configuration change does the following:

- Decrypts the universe key registry with the previous latest universe key.
- Adds the new key to the registry.
- Updates the cluster configuration with the new latest key ID.
- Encrypts the registry with the new key.

Once encryption is enabled with a new key, only new data is encrypted with this new key. Old data will remain unencrypted, or encrypted, with an older key, until compaction churn triggers a
re-encryption with the new key.

To rotate the universe keys, perform the following steps:

1. Open the Yugabyte Platform console and go the the universe.
2. Click the **More** drop-down list and select **Manage Encryption Keys**. The **Manage Keys** window appears.
3. Select **Rotate Key?** and then click **Submit**.

## Disable encryption at rest

To disable encryption at rest, follow these steps:

1. Open the Yugabyte Platform console and go the the universe.
2. Click the **More** drop-down list and select **Manage Encryption Keys**. The **Manage Keys** window appears.
3. Deselect **Enable Encryption-at-Rest for <UNIVERSE_NAME>** and click **Submit**.
4. To verify that encryption at rest is disabled, check the current cluster configuration for each node to see that it contains `encryption_enabled: false`.
