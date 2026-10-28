// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.commissioner.Common.CloudType;
import io.swagger.annotations.ApiModelProperty;
import java.util.regex.Pattern;
import lombok.Data;
import org.apache.commons.lang3.StringUtils;

/**
 * Settings a DB node needs to reach one storage cloud it is not running on.
 *
 * <p>Federation is a property of the pair (cloud the node runs on, cloud the bucket lives in), but
 * only the second half is configuration: the first is a fact about the node, discovered from its
 * metadata service. Keying config by target therefore costs one entry per storage cloud rather than
 * one per ordered pair, which is what lets Azure and OCI be added later without the set of
 * combinations growing on every axis.
 *
 * <p>Same-cloud access is native and needs no entry - a node only ever uses an entry whose {@link
 * #targetCloud} differs from the cloud it runs on.
 */
@Data
@JsonInclude(JsonInclude.Include.NON_NULL)
@JsonIgnoreProperties(ignoreUnknown = true)
public class CrossCloudFederationTarget {

  /**
   * Rejected when a universe is configured, not warned about when a backup runs: a backup snapshots
   * one audience and role ARN, and {@code userIntent.provider} on a multi-provider cluster is
   * backfilled from an arbitrary one, so there is no correct value to record. Enforced by
   * UniverseCRUDHandler (create/edit) and the v2 enable API, which are the only paths that turn
   * federation on for a universe.
   */
  public static final String MULTICLOUD_UNSUPPORTED_ERROR =
      "Cross-cloud federated IAM is not supported on a cluster that spans multiple providers."
          + " Disable federated IAM on the provider, or use a single provider for this cluster.";

  @ApiModelProperty(
      value =
          "Storage cloud this entry grants access to. Only aws (S3) and gcp (GCS) are supported"
              + " today.",
      required = true)
  public CloudType targetCloud;

  @ApiModelProperty(
      value =
          "Token audience. For a gcp target this is the GCP Workload Identity Federation audience"
              + " (//iam.googleapis.com/projects/.../providers/...); for an aws target it is the"
              + " audience requested for the node's web-identity token.",
      required = true)
  public String audience;

  @ApiModelProperty(
      value =
          "AWS role assumed via AssumeRoleWithWebIdentity. Required when targetCloud is aws,"
              + " unused otherwise: reaching GCS binds the node's identity to the bucket directly,"
              + " with no intermediate role.")
  public String roleArn;

  /** Convenience factory, used to fold a provider's legacy flat fields into a target. */
  public static CrossCloudFederationTarget of(
      CloudType targetCloud, String audience, String roleArn) {
    CrossCloudFederationTarget target = new CrossCloudFederationTarget();
    target.targetCloud = targetCloud;
    target.audience = audience;
    target.roleArn = roleArn;
    return target;
  }

  /**
   * Shape of the values a federation target carries, shared by every provider validator so one
   * definition governs what YBA accepts. The ARN is restricted to the standard {@code aws}
   * partition because both YBA and the DB node use the global STS endpoint, which aws-cn and
   * aws-us-gov do not serve. The audience charset excludes quotes, backslashes and whitespace
   * because it is interpolated into the external_account JSON template.
   */
  private static final Pattern ROLE_ARN_PATTERN =
      Pattern.compile("^arn:aws:iam::[0-9]{12}:role/[A-Za-z0-9._/+=,@-]{1,256}$");

  private static final Pattern AUDIENCE_PATTERN = Pattern.compile("^[A-Za-z0-9._:/-]{1,512}$");

  public static boolean isValidRoleArn(String roleArn) {
    return StringUtils.isNotBlank(roleArn) && ROLE_ARN_PATTERN.matcher(roleArn).matches();
  }

  public static boolean isValidAudience(String audience) {
    return StringUtils.isNotBlank(audience) && AUDIENCE_PATTERN.matcher(audience).matches();
  }

  /**
   * Whether this entry carries everything its target needs. An aws target is unusable without the
   * role to assume, so a half-filled entry must not be reported as configured.
   */
  @JsonIgnore
  public boolean isUsable() {
    if (targetCloud == null || StringUtils.isBlank(audience)) {
      return false;
    }
    return targetCloud != CloudType.aws || StringUtils.isNotBlank(roleArn);
  }
}
