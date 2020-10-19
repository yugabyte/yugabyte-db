---
title: Configure KMS
headerTitle: Configure KMS
linkTitle: Configure KMS
description: Configure KMS.
menu:
  latest:
    parent: secure-universes
    identifier: enable-kms
    weight: 10
isTocNested: true
showAsideToc: true
---

## KMS overview

The encryption at rest infrastructure in Yugabyte Platform is designed in an abstracted way to enable easy implementation of third-party KMS integrations.
Support is currently available for Equinix Smartkey and AWS KMS.

Yugabyte Platform does not persist universe keys locally on disk. Only a “key reference” is stored to allow runtime retrieval through API.

To reduce unnecessary API calls to third-party integrations, Yugabyte Platform has an in-memory cache of each universe’s latest universe key.

- integrate with third-party KMS to generate a universe key (default AES-256)
