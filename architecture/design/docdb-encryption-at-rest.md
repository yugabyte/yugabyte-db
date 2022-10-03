# Encryption At Rest in YugabyteDB

Data at rest within a YugabyteDB cluster should be protected from unauthorized users by encrypting it. This document outlines how this is achieved internally, along with what features we support.

## Features

Users will be able to perform the following operations:

* Enable and disable encryption on existing clusters with data.
* Backup encrypted data in efficiently. This should not involve a rewrite of the entire dataset.
* Periodically rotate encryption keys. This operation should be efficient without a rewrite of the entire dataset. Rotation of keys is done to limit the window that an attacker can use the key to access the data.
* Integrate key management with an external KMS system.

## Assumptions

This feature makes the following assumptions:

* Encryption will only be enabled for tablet server data files and WAL files. Other auxillary data such as metadata files (for example, files that store the internal uuid of a tablet server) and logs from various processes are not encrypted. These do not constitute user data.
* Attackers do not have privileged access on a running system, so they cannot read in-memory state.
* Attackers do not have write access to the encrypted raw files. Thus, there is no scheme to verify the integrity of the data in the case that an attacker corrupts it.

# Design

## Basic Concepts

There are two types of keys to encrypt data in YugabyteDB:
* **Universe key**: Top level symmetric key used to encrypt other keys (see data keys below), these are common to the cluster. 
* **Data key**: Symmetric key used to encrypt the data. There is one data key generated per flushed file.

For each universe (or cluster), there is a top level universe key which encrypts a group of data keys. The data keys are responsible for encrypting the actual data. Universe keys are controlled by the user, while data keys are internal to the database.

## Data Key Management 

Data keys are symmetric keys used to encrypt and decrypt the data and are internal to the database. A new data key is created for every new file, and once a file is created, the same data key is used for all reads and writes on that file. By default data keys are 16 bytes. 

We have a fundamental question to answer: where are data keys stored? We have 3 possible solutions, each with pros and cons.

#### Option 1: Store keys in an external registry.

**Pros**: Size of encrypted and plaintext file are the same. Flexibility for where encryption occurs, since anyone can own the registry.

**Cons**: Requires synchronization between file creation/deletion and registry, requires additional logic to create checkpoint.

#### Options 2: Store keys in the file name of the file.

**Pros**: Size of encrypted and plaintext file are the same. Creating a checkpoint requires no additional logic other than a file rename. No synchronization needed.

**Cons**: File names will be clunky, requires filesystem call sites to be file name format aware.

#### Option 3: Store keys in the header of the file.

**Pros**: No synchronization needed, file name format doesn’t change.

**Cons**: Size of encrypted and plaintext file are different, complicates read offsets and file size operations.

**We decided to go with option 3**. External synchronization required for option 1 introduces the potential for race conditions and other concurrency issues and would require a great deal of care. Meanwhile, option 2 is not very extensible as we want to associate more encryption metadata with each file. 

With this approach, it naturally follows that we do encryption at the filesystem level. Each file handler is responsible for encrypting/decrypting its file contents before returning. To create such encrypted files, we will define an `EncryptedFileFactory` object.

> Key Takeaways: One data key per file, each data key lives with the file it encrypts. Chose this approach to avoid external synchronization and filename format change. This means that encryption happens at filesystem layer.

## Universe Keys

Universe keys are located on master and must be rotated by the user. Each universe key has 3 states:
* **Active**: Used to encrypt new data keys
* **In-use**: Encrypts data but not used for any new data.
* **Inactive**: Not in use anymore.

A mapping of version id to universe keys are stored in a registry, encrypted by the latest universe key, in the master's sys catalog table. The latest plaintext universe key is only stored in-memory on all the masters. This universe key is never flushed to disk. What this means is that plaintext keys are never flushed to disk, and all flushed keys are encrypted before flushing.

The universe key is sent to the tablet servers via heartbeat. Anytime a tablet server comes up for the first time or a new key is rotated into the cluster, the new updated registry is sent to the tablet server. The tablet server only keeps this registry in memory, and uses for data file flushes as described above.

> Key Takeaways: Universe keys are managed by the user and must be rotated manually. The latest universe key is stored plaintext in-memory (never flushed). Master owns a registry of universe keys, which is sent to the tablet server via heartbeat.

## Key Rotations

Enabling encryption and rotating a new key works in 2 steps:
* Add the new universe key id and key data to all the masters' in-memory state.
* Issue a cluster config change to enable encryption with the new key.

The cluster config change does the following:
* Decrypts the universe key registry with the previous latest universe key.
* Adds the new key to the registry.
* Updates the cluster config with the new latest key id.
* Encrypts the registry with new key.

Once encryption is enabled with a new key, only new data is encrypted with this new key. Old data will remain unencrypted/encrypted with an older key, until compaction churn triggers a re-encryption with the new key. 

## Master Failures

On master failures, master's lose their in-memory state, which includes the latest universe key. Therefore, we implemented a bootstrap mechanism for the latest key on startup. Master sends a broadcast request for the latest key to the other masters in the cluster with the assumption that at least one other master has the latest key. If the entire cluster is starting up or encryption is not enabled, this request is a no-op.

If all the masters fail simultaneously (e.g. a non-rolling restart), none of the masters will have the latest key in memory. For this situation, YW has a periodic task to pass the latest universe key to a cluster. If the cluster was created manually without YW, then the user will need to use our yb-admin to populate each master's in-memory state.

## Adding New Node

The process of hydrating a new node with the necessary data is called **remote bootstrap**.

When a remote bootstrapping node requests files, the source node reads the data from its files into a buffer and sends it across the network. Assuming that the data is encrypted, if reads are done via an encrypted file, the resulting data sent over the network will be un-encrypted. To preserve data encryption, we want to do a file copy without any data modification, so we must use a plaintext file. We denote plaintext files of tablet data (encrypted or plaintext) checkpoint files, used for creating checkpoints of outbound data.

## Backup and Restore

When backing up a cluster, we could end up restoring to a different cluster with a different universe key/no encryption enabled. Therefore, backup must include the decrypted data keys for all files.

We consider two options to achieve this:

### Option 1

If KMS integration is enabled, include the master universe key registry, encrypted with a KMS key, as part of the backup with the encrypted data files. On restore, we decrypt the registry with this KMS key, and this registry is populated into the master sys catalog and used to read the backed-up data files.

### Option 2

If KMS is not used, backup just the encrypted data files. On restore, use an out of band method to replay the universe key history to repopulate the registry. Once this is done, the cluster will be able to read the restored data files.

While we will eventually support option 1, we currently don't have a mechanism to include master metadata in backup. Therefore, we use option 2 to support backups as of now.

# Key Management Service (KMS) Integration

KMS integration would initially be facilitated via the Enterprise Platform solution, where the user would maintain the credentials to their KMS system of choice, and at the universe creation time
we would make appropriate API calls to create a new Universe Key and use that key to provision a new universe with At Rest Encryption enabled. In this section, we detail the approach we plan on
taking with some of the KMS system that we would support via Platform. 

## Equinix [SmartKey](https://www.equinix.com/services/edge-services/smartkey/). Integration
  SmartKey is a KMS offering from Equinix, they provide SDK and API to manage the keys in their platform, Yugabyte platform would integrate with SmartKey via the REST API route and authenticate
using their API key in order to manage the Keys. We would use the name attribute on the Key to link the universe that the key is generated for. Once the key is generated we would make appropriate RPC 
calls to YugabyteDB to enable encryption. We would call their rekey api when the user wants to rekey the universe and update the YugabyteDB nodes in a rolling fashion. 

## AWS [Key Management Service](https://docs.aws.amazon.com/kms/latest/developerguide/overview.html)
  Amazon offers their KMS solution, we will use their KMS API to manage the keys. And they have the concept of aliases which we would use to build a relationship between the key and universe.
When the key needs to be rotated we would create a new key and update the alias accordingly. And do the update on YugabyteDB nodes in a rolling fashion.

# Implementation Internals

## AES CTR Mode

Read more about [the AES CTR mode](https://en.wikipedia.org/wiki/Block_cipher_mode_of_operation#Counter_(CTR)). We support CTR-128, CTR-196 and CTR-256 modes of the encryption. The numbers refer to the key size in bits, so CTR-128 mode uses a 16 byte key. All modes have a block size of 16-bytes.

Encryption in CTR-128 mode works in the following steps:

* Generate a random 16 byte key, and 16 byte block initialization vector (`iv`).
    * First 12 bytes of `iv` are the nonce, last 4 bytes are the counter.
* For the first block:
    * take the `iv`, encrypt it using the key
    * xor the result with the first 16-byte data block to be encoded
* For each subsequent block:
    * increment the counter by 1 and repeat the steps for the first block.

### A note on performance

Since the final operation to convert plaintext to ciphertext (and vice versa) is an xor, there is a 1 to 1 mapping between plaintext and ciphertext even when data is not block aligned. This greatly simplifies point reads, since a read at point n in plaintext is a read at point n in ciphertext. This performance efficiency is why we choose to support CTR mode of encryption.

> Key Takeaways: CTR mode of encryption preserves the size of the plaintext for any size of data.

## Encrypted File Format

Encrypted files have the header format:

```proto
Header {
  Encryption Magic (8 bytes)
  Header Size (4 bytes)
  Serialized EncryptionHeaderPB
}

message EncryptionHeaderPB {
 optional bytes data_key = 1;
 optional bytes data_nonce = 2;
 optional int32 data_counter = 3; 
 optional bytes universe_key_version = 4;
}
```

We only store the universe key version in the file header, so the actual universe key data will never be persisted with the tablet data!

An `Env` object sits between YugabyteDB and the filesystem and is responsible for creating new files. YugabyteDB has the notion of an encrypted and plaintext Env, corresponding to the type of file it creates. The encrypted Env owns an object that listens on heartbeat and is responsible for fetching universe keys. The encrypted Env creates new files as follows:

### Writable File Creation

* Create a new data key.
* Fetch the latest universe key.
* Encrypt the data key with the universe key and append a file header.
* Pass the decrypted data key to the new file.

### Readable File Creation

* From the universe key version in the header, fetch the corresponding universe key.
* Decrypt the data key and pass it to the new file.

> Key Takeaways: Env layer is responsible for creating files and owns object that retrieves universe keys from heartbeat.

## File Operations

All files own their data key, so appending to a file encrypts the new data using that data key and then calls the underlying append implementation. However, reads at an offset are more complicated because of the file header.

### Reading at an Offset

Since the nth byte of tablet data in an encrypted file corresponds to offset n + header size in the file, encrypted files are responsible for adding header size to all read offsets. We decide to do it at the file layer because callers should not have to understand encrypted file format or even understand that the file they want to read is encrypted. It is the file’s responsibility to know its own header size and offset accordingly. 

> Key Takeaways: Reading at an offset requires the file Read implementation to account for header size.

# Future Work

* Enable using a KMIP server for the universe key
* Make Yugabyte platform act like a KMIP server which would encapsulate different KMS systems and give one common interface for YugabyteDB to interact.

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/docdb-encryption-at-rest.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)
