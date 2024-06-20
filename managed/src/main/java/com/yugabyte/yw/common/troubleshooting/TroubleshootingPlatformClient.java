package com.yugabyte.yw.common.troubleshooting;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.forms.TroubleshootingPlatformExt;
import com.yugabyte.yw.models.TroubleshootingPlatform;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Data;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.libs.ws.WSClient;

@Singleton
@Slf4j
public class TroubleshootingPlatformClient {

  public static final String WS_CLIENT_KEY = "yb.troubleshooting.ws";
  private final WSClientRefresher wsClientRefresher;

  @Inject
  public TroubleshootingPlatformClient(WSClientRefresher wsClientRefresher) {
    this.wsClientRefresher = wsClientRefresher;
  }

  public CustomerMetadata putCustomerMetadata(TroubleshootingPlatform platform) {
    String customerMetadataUrl =
        platform.getTpUrl() + "/api/customer_metadata/" + platform.getCustomerUUID();
    try {
      CustomerMetadata customerMetadata =
          new CustomerMetadata()
              .setId(platform.getCustomerUUID())
              .setPlatformUrl(platform.getYbaUrl())
              .setMetricsUrl(platform.getMetricsUrl())
              .setMetricsScrapePeriodSec(platform.getMetricsScrapePeriodSecs())
              .setApiToken(platform.getApiToken());
      JsonNode result =
          getApiHelper().putRequest(customerMetadataUrl, Json.toJson(customerMetadata));
      if (result.has("error")) {
        throw new RuntimeException("Error response received " + result.get("error"));
      }
      return Json.fromJson(result, CustomerMetadata.class);
    } catch (Exception e) {
      log.error("Failed to put customer metadata at " + customerMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to put customer metadata at " + customerMetadataUrl);
    }
  }

  public void deleteCustomerMetadata(TroubleshootingPlatform platform) {
    String customerMetadataUrl =
        platform.getTpUrl() + "/api/customer_metadata/" + platform.getCustomerUUID();
    try {
      JsonNode result = getApiHelper().deleteRequest(customerMetadataUrl);
      if (result.has("error")) {
        throw new RuntimeException("Error response received " + result.get("error"));
      }
    } catch (Exception e) {
      log.error("Failed to delete customer metadata at " + customerMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to delete customer metadata at " + customerMetadataUrl);
    }
  }

  public UniverseMetadata getUniverseMetadata(TroubleshootingPlatform platform, UUID universeUuid) {
    String universeMetadataUrl = platform.getTpUrl() + "/api/universe_metadata";
    try {
      JsonNode result =
          getApiHelper()
              .getRequest(
                  universeMetadataUrl,
                  Collections.emptyMap(),
                  ImmutableMap.of(
                      "customer_uuid", platform.getCustomerUUID().toString(),
                      "universe_uuid", universeUuid.toString()));
      if (result.has("error")) {
        throw new RuntimeException("Error response received " + result.get("error"));
      }
      if (!result.isArray()) {
        throw new RuntimeException("Unexpected response received " + result);
      }
      if (result.isEmpty()) {
        return null;
      } else {
        return Json.fromJson(((ArrayNode) result).get(0), UniverseMetadata.class);
      }
    } catch (Exception e) {
      log.error("Failed to get universe metadata from " + universeMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to get universe metadata from " + universeMetadataUrl);
    }
  }

  public UniverseMetadata putUniverseMetadata(
      TroubleshootingPlatform platform, UniverseMetadata universeMetadata) {
    String universeMetadataUrl =
        platform.getTpUrl() + "/api/universe_metadata/" + universeMetadata.getId();
    try {
      JsonNode result =
          getApiHelper().putRequest(universeMetadataUrl, Json.toJson(universeMetadata));
      if (result.has("error")) {
        throw new RuntimeException("Error response received " + result.get("error"));
      }
      return Json.fromJson(result, UniverseMetadata.class);
    } catch (Exception e) {
      log.error("Failed to put universe metadata at " + universeMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to put universe metadata at " + universeMetadataUrl);
    }
  }

  public void deleteUniverseMetadata(TroubleshootingPlatform platform, UUID universeUuid) {
    String universeMetadataUrl = platform.getTpUrl() + "/api/universe_metadata/" + universeUuid;
    try {
      JsonNode result = getApiHelper().deleteRequest(universeMetadataUrl);
      if (result.has("error")) {
        throw new RuntimeException("Error response received " + result.get("error"));
      }
    } catch (Exception e) {
      log.error("Failed to delete universe metadata at " + universeMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to delete universe metadata at " + universeMetadataUrl);
    }
  }

  public TroubleshootingPlatformExt.InUseStatus getInUseStatus(TroubleshootingPlatform platform) {
    String universeMetadataUrl = platform.getTpUrl() + "/api/universe_metadata";
    try {
      JsonNode universeMetadataList =
          getApiHelper()
              .getRequest(
                  universeMetadataUrl,
                  Collections.emptyMap(),
                  ImmutableMap.of("customer_uuid", platform.getCustomerUUID().toString()));
      if (!universeMetadataList.isArray()) {
        return TroubleshootingPlatformExt.InUseStatus.ERROR;
      }
      return universeMetadataList.isEmpty()
          ? TroubleshootingPlatformExt.InUseStatus.NOT_IN_USE
          : TroubleshootingPlatformExt.InUseStatus.IN_USE;
    } catch (Exception e) {
      return TroubleshootingPlatformExt.InUseStatus.ERROR;
    }
  }

  private ApiHelper getApiHelper() {
    WSClient wsClient = wsClientRefresher.getClient(WS_CLIENT_KEY);
    return new ApiHelper(wsClient);
  }

  @Data
  @Accessors(chain = true)
  public static class CustomerMetadata {
    private UUID id;
    private String apiToken;
    String platformUrl;
    String metricsUrl;
    long metricsScrapePeriodSec;
  }

  @Data
  @Accessors(chain = true)
  public static class UniverseMetadata {
    private UUID id;
    private UUID customerId;
    List<String> dataMountPoints;
    List<String> otherMountPoints;
  }
}
