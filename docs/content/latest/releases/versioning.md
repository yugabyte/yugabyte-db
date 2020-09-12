---
title: Versioning (stable and latest releases)
headerTitle: YugabyteDB Versioning (stable and latest releases)
linkTitle: Versioning (stable and latest)
description: Explains the new release versioning approach for latest and stable releases.
section: RELEASES
menu:
  latest:
    identifier: whats-new
    weight: 2588 
---

{{< note title="Important" >}}

For production deployments, always upgrade to the latest *stable* version.

{{< /note >}}

Starting with v2.2, YugabyteDB versioning has the form `X.Y.Z` where `X.Y`
refers to either a *stable* release series or the *latest* release series and `Z`
refers to the revision/patch number.

For example, in YugabyteDB version `2.2.2.1`, ``2.2`` refers to a stable
release series and `.2.1` refers to the revision.

## Latest release series (`/latest`)

If ``Y`` is odd, ``X.Y`` refers to a development series; for example,
  ``4.1`` development series and ``4.3`` development series.
  Development series are **for testing only and not for production**.

Changes in the release series (e.g. ``4.0`` to ``4.2``) generally mark
the introduction of new features that may break backwards compatibility.

The goal for documentation for the latest releases is to include documentation that has reached "BETA" status.

## Stable release series

- If ``Y`` is even, ``X.Y`` refers to a stable release series; for example,
  ``4.0`` release series and ``4.2`` release series. Release series are
  **stable** and suitable for production.