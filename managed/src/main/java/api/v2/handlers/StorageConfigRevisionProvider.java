// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import com.google.common.hash.Hashing;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.http.conditional.ConditionalGets;
import com.yugabyte.yw.common.http.conditional.ResourceRevision;
import com.yugabyte.yw.common.http.conditional.ResourceRevisionProvider;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.inject.Singleton;
import play.libs.Json;

/**
 * Storage config revision for the ETag of the v2 storage config GET and the If-Match of its edit:
 * {@code customer_config.update_time}, and what uses the config (info.usage), which backups,
 * schedules and DR configs change without touching the config's row. An edit whose If-Match is from
 * before a change of either is answered 412.
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
    return Optional.ofNullable(config).map(StorageConfigRevisionProvider::unscoped);
  }

  /** The revision of a loaded config, as the GET's ETag carries it. */
  public static ResourceRevision revisionOf(CustomerConfig config) {
    return ConditionalGets.scopedToYbaVersion(unscoped(config), Util.getYbaVersion());
  }

  /** Whether an If-Match header matches the config's current revision. */
  public static boolean matchesIfMatch(@Nullable String ifMatch, CustomerConfig config) {
    // If-Match takes the same list of ETags (or *) as If-None-Match.
    return ConditionalGets.matchesIfNoneMatch(ifMatch, revisionOf(config).etag());
  }

  private static ResourceRevision unscoped(CustomerConfig config) {
    UUID uuid = config.getConfigUUID();
    String usage =
        Json.stringify(
            Json.toJson(StorageConfigUsages.of(config.getCustomerUUID(), List.of(uuid)).get(uuid)));
    String usageHash =
        Hashing.sha256().hashString(usage, StandardCharsets.UTF_8).toString().substring(0, 16);
    String etag =
        String.format(
            "W/\"%s:%s:%d:%s\"", RESOURCE_TYPE, uuid, config.getUpdateTime().getTime(), usageHash);
    return new ResourceRevision(etag, config.getUpdateTime());
  }
}
