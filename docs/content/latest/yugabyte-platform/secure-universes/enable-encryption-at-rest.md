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

## KMS overview

The encryption at rest infrastructure in Yugabyte Platform is designed in an abstracted way to enable easy implementation of third-party KMS integrations.
Support is currently available for Equinix Smartkey and AWS KMS.

Yugabyte Platform does not persist universe keys locally on disk. Only a “key reference” is stored to allow runtime retrieval through API.

To reduce unnecessary API calls to third-party integrations, Yugabyte Platform has an in-memory cache of each universe’s latest universe key.

- integrate with third-party KMS to generate a universe key (default AES-256)

## Enable encryption at rest

To enable encryption at rest, follow these steps:

1. Open the Yugabyte Platform console, click **Universes**, and select the universe.
2. Click **More** drop-down list and select **Manage Encryption Keys**. The Manage Keys window appears.
3. Select the radio button to **Enable Encryption-at-Rest for <your-cluster>** and then click **Submit**.

## Rotate universe key

## Disable encryption at rest

To disable encryption at rest, follow these steps.

1. Go to the **Universe Overview** page.
2. 


## Back up and restore data from encrypted at rest universe

## Periodically send latest universe key to masters

- ensures that the masters have the latest universe key in memory

