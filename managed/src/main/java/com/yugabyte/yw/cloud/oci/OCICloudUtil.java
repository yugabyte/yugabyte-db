// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.oci;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.UUID;
import javax.annotation.Nullable;
import org.apache.commons.collections4.MapUtils;

/**
 * Static helpers for OCI cloud operations. OCI allows at most 10 freeform tags per resource;
 * YugabyteDB adds mandatory system tags during provisioning on top of user-provided tags.
 */
public final class OCICloudUtil {

  public static final int MAX_FREEFORM_TAGS = 10;

  private static final Set<String> YBA_MANDATORY_TAG_KEYS =
      ImmutableSet.of(
          "customer-uuid",
          "universe-uuid",
          "node-uuid",
          "yb-server-type",
          "Name",
          "yb_user_email",
          "yb_yba_url");

  private OCICloudUtil() {}

  /**
   * Computes the distinct freeform tag keys that would be applied to an OCI instance after merging
   * user tags with the tags YugabyteDB adds during provisioning.
   */
  public static Set<String> computeEffectiveTagKeys(@Nullable Map<String, String> userTags) {
    Set<String> tagKeys = new TreeSet<>();
    if (MapUtils.isNotEmpty(userTags)) {
      tagKeys.addAll(userTags.keySet());
    }
    tagKeys.addAll(YBA_MANDATORY_TAG_KEYS);
    return tagKeys;
  }

  /**
   * Computes the number of distinct freeform tags that would be applied to an OCI instance after
   * merging user tags with the tags YugabyteDB adds during provisioning.
   */
  public static int computeEffectiveTagCount(@Nullable Map<String, String> userTags) {
    return computeEffectiveTagKeys(userTags).size();
  }

  /**
   * Returns an error message when the effective tag count exceeds {@link #MAX_FREEFORM_TAGS}, or
   * {@code null} when the tag set is valid.
   */
  @Nullable
  public static String getTagCountValidationError(@Nullable Map<String, String> userTags) {
    Set<String> tagKeys = computeEffectiveTagKeys(userTags);
    if (tagKeys.size() <= MAX_FREEFORM_TAGS) {
      return null;
    }
    return String.format(
        "OCI supports at most %d freeform tags per resource, but provisioning would apply %d "
            + "tags (%s). Reduce the number of instance tags by at least %d.",
        MAX_FREEFORM_TAGS,
        tagKeys.size(),
        String.join(", ", tagKeys),
        tagKeys.size() - MAX_FREEFORM_TAGS);
  }

  /**
   * Validates OCI instance tag counts for all OCI providers referenced by the given clusters.
   *
   * @throws PlatformServiceException if any cluster would exceed the OCI tag limit
   */
  public static void validateInstanceTags(List<UniverseDefinitionTaskParams.Cluster> clusters) {
    for (UniverseDefinitionTaskParams.Cluster cluster : clusters) {
      UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;
      for (UUID providerUUID : userIntent.getAllProviderUUIDs()) {
        Provider provider = Provider.getOrBadRequest(providerUUID);
        if (provider.getCloudCode() != Common.CloudType.oci) {
          continue;
        }
        Map<String, String> userTags = userIntent.getInstanceTagsForProvider(providerUUID);
        String error = getTagCountValidationError(userTags);
        if (error != null) {
          throw new PlatformServiceException(BAD_REQUEST, error);
        }
      }
    }
  }
}
