# Encryption At Rest in YugaByte DB

Data at rest within a YugaByte DB cluster should be protected from unauthorized users by encrypting it. This document outlines how this is achieved internally, along with what features it supports.

## Features

Users want to be able to perform the following operations:

* Enable and disable encryption on existing clusters with data.
* Encrypt some tables (but not others).
* Backup data in an unencrypted format efficiently. This should not involve a rewrite of the entire dataset.
* Periodically rotate encryption keys. This operation should be efficient without a rewrite of the entire dataset. Rotation of keys is done to limit the window that an attacker can use the key to access the data.
* Integrate key management with an external KMS system.

Note the following points:
* To preserve keys and data in memory, we disable core dump.
* We will use the openssl EVP library for encryption operations.
* This feature is about filesystem level encryption of data stored in YugaByte DB. Therefore, features such as fine-grained encryption (e.g. at the column level) is intentionally not a design goal of this feature. 


## Assumptions

This feature makes the following assumptions:
* Encryption will only be enabled for tablet server data files and WAL files. Other auxillary data such as metadata files (for example, files that store the internal uuid of a tablet server) and logs from various processes are not encrypted. These do not constitute user data.
* Attackers do not have privileged access on a running system, so they cannot read memory or universe keys.
* Attackers do not have write access to the encrypted raw files. Thus, there is no scheme to verify the integrity of the data in the case that an attacker corrupts it.


# Design

## Basic Concepts

There are two types of keys to encrypt data in YugaByte DB:
* **Universe key**: Top level symmetric key used to encrypt other keys (see data keys below), these are common to the cluster. 
* **Data key**: Symmetric key used to encrypt the data. There is one data key generated per file.

For each universe (or cluster), there is a top level universe key which encrypts a group of data keys. The data keys are responsible for encrypting the actual data. Universe keys are controlled by the user, while data keys are internal to the database.

> In a future iteration, the user can use a KMIP server for the universe key. 

## AES CTR Mode

Read more about [the AES CTR mode](https://en.wikipedia.org/wiki/Block_cipher_mode_of_operation#Counter_(CTR)). We support CTR-128, CTR-196 and CTR-256 modes of the encryption. The numbers refer to the key size in bytes, so CTR-128 mode uses a 16 byte key. All modes have a block size of 16-bytes.

Encryption in CTR-128 mode works in the following steps:

* Generate a random 16 byte key, and 16 byte block initialization vector (`iv`).
    * First 12 bytes of `iv` are the nonce, last 4 bytes are the counter.
* For the first block:
    * take the `iv`, encrypt it using the key
    * xor the result with the first 16-byte data block to be encoded
* For each subsequent block:
    * increment the counter by 1 and repeat the steps for the first block.

#### A note on performance
Since the final operation to convert plaintext to ciphertext (and vice versa) is an xor, there is a 1 to 1 mapping between plaintext and ciphertext even when data is not block aligned. This greatly simplifies point reads, since a read at point n in plaintext is a read at point n in ciphertext. This performance efficiency is why we choose to support CTR mode of encryption.

> Key Takeaways: CTR mode of encryption preserves the size of the plaintext for any size of data.

Data Keys

Data keys are symmetric keys used to encrypt and decrypt the data and are internal to the database. A new data key is created for every new file, and once a file is created, the same data key is used for all reads and writes on that file. By default data keys are 16 bytes, so will use CTR-128. 

## Data Key Management 

We have a fundamental question to answer: who manages data keys? We have 3 possible solutions, each with pros and cons.

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

A mapping of version numbers to universe keys are stored in a registry, encrypted by the latest universe key, in the sys catalog table. The universe key registry looks as follows:

```proto
message UniverseKeyRegistryPB {
    optional bool encryption_enabled = 1 [default = false];
    message UniverseKeyEntryPB {
        optional int32 version_number = 1;
        optional bytes key = 2;
        optional bytes nonce = 3;
        optional int32 counter = 4;
    }
    repeated UniverseKeyEntryPB universe_keys = 2;
}
```
Whenever a new key is rotated, the following happens: 
* Retrieves the latest registry and decrypt it using the previous latest key.
* Adds the new key to universe_keys.
* Updates the cluster config with the path of the new key.
* Encrypts the registry with new key, and flushes the encrypted registry.

Disabling encryption with get and decrypt the registry, sets encryption_enabled to false, and then flushes the decrypted registry. New universe keys are propagated to the tablet server through heartbeat. For each tablet server, master owns state telling it whether the universe key registry has changed or not since last heartbeat to avoid sending the same registry. The universe key size can be either 16, 24, or 32 bytes to be chosen by the user, and dictates the encryption mode used for encrypting data keys.

> Key Takeaways: Universe keys are managed by the user and must be rotated manually. Master owns a registry of universe keys, which is sent to the tablet server via heartbeat.

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

It’s important that only the universe key version is stored in the file header. The universe key must never be persisted with the tablet data!

An `Env` object sits between YugaByte and the filesystem and is responsible for creating new files. YugaByte has the notion of an encrypted and plaintext Env, corresponding to the type of file it creates. The encrypted Env owns an object that listens on heartbeat and is responsible for fetching universe keys. The encrypted Env creates new files as follows:

#### Writable File Creation
* Create a new data key.
* Fetch the latest universe key.
* Encrypt the data key with the universe key and append a file header.
* Pass the decrypted data key to the new file.

#### Readable File Creation
* From the universe key version in the header, fetch the corresponding universe key.
* Decrypt the data key and pass it to the new file.

> Key Takeaways: Env layer is responsible for creating files and owns object that retrieves universe keys from heartbeat.

## File Operations

All files own their data key, so appending to a file encrypts the new data using that data key and then calls the underlying append implementation. However, reads at an offset are more complicated because of the file header.

#### Reading at an Offset

Since the nth byte of tablet data in an encrypted file corresponds to offset n + header size in the file, encrypted files are responsible for adding header size to all read offsets. We decide to do it at the file layer because callers should not have to understand encrypted file format or even understand that the file they want to read is encrypted. It is the file’s responsibility to know its own header size and offset accordingly. 

> Key Takeaways: Reading at an offset requires the file Read implementation to account for header size.


# Interplay with other operations

## Adding New Node

The process of hydrating a new node with the necessary data is called **remote bootstrap**.

When a remote bootstrapping node requests files, the source node reads the data from its files into a buffer and sends it across the network. Assuming that the data is encrypted, if reads are done via an encrypted file, the resulting data sent over the network will be un-encrypted. To preserve data encryption, we want to do a file copy without any data modification, so we must use a plaintext file. We denote plaintext files of tablet data (encrypted or plaintext) checkpoint files, used for creating checkpoints of outbound data.

## Backup and Restore

When backing up a cluster, we could end up restoring to a different cluster with a different universe key/no encryption enabled. Therefore, backup must include the decrypted data keys for all files.

We considered three options to achieve this:

#### Option 1
If we keep this flow, a backup would include the raw files + an encryption metadata file that would include all the decrypted data keys. This file can be encrypted with an additional backup key, similar to a universe key. On restore, the header on each file must be modified in place from the data key encrypted by uk_old to the data key encrypted by uk_new.

#### Option 2
Additionally, could decide to split the header with encryption metadata into a separate file. On backup, each file f would come with a f.plain file, containing the plaintext data key for f. On restore, create a new file f.enc which contains the data key in f.plain encrypted by uk_new. On the plus side, this avoids in-place modification logic since we create a new file. However, would require us to augment our checkpoint logic to include these new encryption metadata files.

#### Option 3
If we want to both keep the checkpoint logic the same while avoiding in place modification of files, could introduce notion of table level keys. Table keys would be responsible for encrypting data keys, and the registry of table keys is kept in syscatalog, encrypted by the latest universe key. A backup would include the raw files with a plaintext table key registry. After backup, the table level keys would be rotated so that attackers who gain access to the backed up table level keys cannot read any future data. During restore, the file headers do not have to change since the table keys used to encrypt the data keys are also imported. The imported table keys registry would be encrypted by uk_new and added to the existing registry. 

**We picked option 3 as that was the cleanest design**. We avoid in place modification of files and keep the file and its encryption metadata, located in the header as discussed above, isolated to one file. In addition, table level encryption naturally follows from table level keys, so we would get that for free.

# Key Management Service (KMS) Integration

KMS integration would initially be facilitated via the Enterprise Platform solution, where the user would maintain the credentials to their KMS system of choice, and at the universe creation time
we would make appropriate API calls to create a new Universe Key and use that key to provision a new universe with At Rest Encryption enabled. In this section we details the approach we plan on 
taking with some of the KMS system that we would support via Platform. 

## Equinix [SmartKey](https://www.equinix.com/services/edge-services/smartkey/). Integration
  SmartKey is KMS a offering from Equinix, they provide SDK and API to manage the keys in their platform, YugaByte platform would integrate with SmartKey via the REST API route and authenticate
using their API key in order to manage the Keys. We would use the name attribute on the Key to link the universe that the key is generated for. Once the key is generated we would make appropriate RPC 
calls to YugaByte to enable encryption. We would call their rekey api when the user wants to rekey the universe and update the YugaByte nodes in a rolling fashion. 

## AWS [Key Management Service](https://docs.aws.amazon.com/kms/latest/developerguide/overview.html)
  Amazon offers their KMS solution, we will your their KMS api to manage the keys, And they have the concept of aliases which we would use that to build a relationship between the key and universe.
When the key needs to be rotated we would create a new key and update the alias accordingly. And do the update on YugaByte nodes in a rolling fashion.

# Future Work

* Enable using a KMIP server for the universe key
* Make YugaByte platform act like a KMIP server which would encapsulate different KMS systems and give one common interface for YugaByte to interact.

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/docdb-encryption-at-rest.md?pixel&useReferer)](https://github.com/YugaByte/ga-beacon)
