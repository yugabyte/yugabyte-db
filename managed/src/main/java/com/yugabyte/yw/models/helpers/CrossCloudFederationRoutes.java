// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.Common.CloudType;
import java.util.Collections;
import java.util.Map;
import java.util.Set;

/**
 * The (node cloud -> storage cloud) pairs cross-cloud federated IAM can serve.
 *
 * <p>Not every pair of clouds is supported. Every route needs its own identity exchange, so a node
 * pointed at a bucket it has no route to has no credentials for it, and a backup from it would fail
 * later with an unrelated error. Where the provider fixes the node's cloud, that is refused when
 * the provider is saved: an OCI provider cannot name an S3 target. An on-prem provider's nodes can
 * be on any cloud, so there a node with no route to a configured target is skipped with a warning
 * when federation is configured, which leaves it as it was before the pair could be named. GCP is
 * one end of every route.
 */
public final class CrossCloudFederationRoutes {

  private static final Map<CloudType, Set<CloudType>> ROUTES =
      ImmutableMap.of(
          // GCS-on-AWS: the node presents its AWS identity to GCP Workload Identity Federation.
          CloudType.aws, ImmutableSet.of(CloudType.gcp),
          // S3-on-GCP: the node exchanges its GCE token for AWS credentials.
          CloudType.gcp, ImmutableSet.of(CloudType.aws),
          // GCS-on-OCI: the node's instance principal is exchanged at an OCI identity domain for a
          // JWT that GCP Workload Identity Federation trusts.
          CloudType.oci, ImmutableSet.of(CloudType.gcp));

  private CrossCloudFederationRoutes() {}

  /** Whether a node on {@code source} has a federation route to a bucket on {@code target}. */
  public static boolean isSupported(CloudType source, CloudType target) {
    return supportedTargets(source).contains(target);
  }

  /** Whether {@code cloud} can be the source of any federation route. */
  public static boolean isSource(CloudType cloud) {
    return ROUTES.containsKey(cloud);
  }

  /** The storage clouds a node on {@code source} can reach by federation; empty when none. */
  public static Set<CloudType> supportedTargets(CloudType source) {
    return ROUTES.getOrDefault(source, Collections.emptySet());
  }
}
