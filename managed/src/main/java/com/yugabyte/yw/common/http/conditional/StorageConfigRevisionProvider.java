// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.http.conditional;

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import java.util.Optional;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Singleton;

/**
 * Storage config revision from {@code customer_config.update_time}, for the ETag of the v2 storage
 * config GET and the If-Match of its edit.
 */
@Singleton
public class StorageConfigRevisionProvider implements ResourceRevisionProvider {

  public static final String RESOURCE_TYPE = "storage-config";

  @Override
  public Optional<ResourceRevision> find(UUID customerUUID, UUID resourceUUID) {
    CustomerConfig config =
        CustomerConfig.find
            .query()
            .select("configUUID, customerUUID, type, updateTime")
            .where()
            .eq("configUUID", resourceUUID)
            .eq("customerUUID", customerUUID)
            .eq("type", ConfigType.STORAGE)
            .findOne();
    // ConditionalGetAction scopes it to the YBA version.
    return Optional.ofNullable(config)
        .map(
            c ->
                ConditionalGets.fromTimestamp(RESOURCE_TYPE, c.getConfigUUID(), c.getUpdateTime()));
  }

  /** The revision of a loaded config, as the GET's ETag carries it. */
  public static ResourceRevision revisionOf(CustomerConfig config) {
    return ConditionalGets.scopedToYbaVersion(
        ConditionalGets.fromTimestamp(
            RESOURCE_TYPE, config.getConfigUUID(), config.getUpdateTime()),
        Util.getYbaVersion());
  }

  /** Whether an If-Match header matches the config's current revision. */
  public static boolean matchesIfMatch(@Nullable String ifMatch, CustomerConfig config) {
    // If-Match takes the same list of ETags (or *) as If-None-Match.
    return ConditionalGets.matchesIfNoneMatch(ifMatch, revisionOf(config).etag());
  }
}
