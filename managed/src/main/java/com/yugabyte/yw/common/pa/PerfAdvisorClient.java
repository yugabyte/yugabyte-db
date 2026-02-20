package com.yugabyte.yw.common.pa;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.forms.PACollectorExt;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsFormat;
import java.io.File;
import java.time.Instant;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Data;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;
import play.libs.ws.WSClient;

@Singleton
@Slf4j
public class PerfAdvisorClient {

  public static final String WS_CLIENT_KEY = "yb.pa.ws";
  public static final String TP_API_TOKEN_HEADER = "X-AUTH-TP-API-TOKEN";
  private final WSClientRefresher wsClientRefresher;

  @Inject
  public PerfAdvisorClient(WSClientRefresher wsClientRefresher) {
    this.wsClientRefresher = wsClientRefresher;
  }

  public CustomerMetadata putCustomerMetadata(PACollector collector) {
    String customerMetadataUrl =
        collector.getPaUrl() + "/api/customer/" + collector.getCustomerUUID() + "/metadata";
    try {
      CustomerMetadata customerMetadata =
          new CustomerMetadata()
              .setId(collector.getCustomerUUID())
              .setPlatformUrl(collector.getYbaUrl())
              .setMetricsUrl(collector.getMetricsUrl())
              .setMetricsUsername(collector.getMetricsUsername())
              .setMetricsPassword(collector.getMetricsPassword())
              .setMetricsScrapePeriodSec(collector.getMetricsScrapePeriodSecs())
              .setApiToken(collector.getApiToken());
      JsonNode result =
          getApiHelper()
              .putRequest(
                  customerMetadataUrl, Json.toJson(customerMetadata), authHeader(collector));
      handleError(result);
      return Json.fromJson(result, CustomerMetadata.class);
    } catch (Exception e) {
      log.error("Failed to put customer metadata at " + customerMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to put customer metadata at " + customerMetadataUrl);
    }
  }

  public void deleteCustomerMetadata(PACollector collector) {
    String customerMetadataUrl =
        collector.getPaUrl() + "/api/customer/" + collector.getCustomerUUID() + "/metadata";
    try {
      JsonNode result = getApiHelper().deleteRequest(customerMetadataUrl, authHeader(collector));
      handleError(result);
    } catch (Exception e) {
      log.error("Failed to delete customer metadata at " + customerMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to delete customer metadata at " + customerMetadataUrl);
    }
  }

  public UniverseMetadata getUniverseMetadata(PACollector collector, UUID universeUuid) {
    String universeMetadataUrl = collector.getPaUrl() + "/api/universe/metadata";
    try {
      JsonNode result =
          getApiHelper()
              .getRequest(
                  universeMetadataUrl,
                  authHeader(collector),
                  ImmutableMap.of(
                      "customer_uuid", collector.getCustomerUUID().toString(),
                      "universe_uuid", universeUuid.toString()));
      handleError(result);
      if (!result.isArray()) {
        throw new RuntimeException("Unexpected response received " + result);
      }
      if (result.isEmpty()) {
        return null;
      } else {
        return Json.fromJson((result).get(0), UniverseMetadata.class);
      }
    } catch (Exception e) {
      log.error("Failed to get universe metadata from " + universeMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to get universe metadata from " + universeMetadataUrl);
    }
  }

  public UniverseMetadata putUniverseMetadata(
      PACollector collector, UniverseMetadata universeMetadata) {
    String universeMetadataUrl =
        collector.getPaUrl() + "/api/universe/" + universeMetadata.getId() + "/metadata";
    try {
      JsonNode result =
          getApiHelper()
              .putRequest(
                  universeMetadataUrl, Json.toJson(universeMetadata), authHeader(collector));
      handleError(result);
      return Json.fromJson(result, UniverseMetadata.class);
    } catch (Exception e) {
      log.error("Failed to put universe metadata at " + universeMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to put universe metadata at " + universeMetadataUrl);
    }
  }

  public void deleteUniverseMetadata(PACollector collector, UUID universeUuid) {
    String universeMetadataUrl =
        collector.getPaUrl() + "/api/universe/" + universeUuid + "/metadata";
    try {
      JsonNode result = getApiHelper().deleteRequest(universeMetadataUrl, authHeader(collector));
      handleError(result);
    } catch (Exception e) {
      log.error("Failed to delete universe metadata at " + universeMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to delete universe metadata at " + universeMetadataUrl);
    }
  }

  public PACollectorExt.InUseStatus getInUseStatus(PACollector collector) {
    String universeMetadataUrl = collector.getPaUrl() + "/api/universe/metadata";
    try {
      JsonNode universeMetadataList =
          getApiHelper()
              .getRequest(
                  universeMetadataUrl,
                  authHeader(collector),
                  ImmutableMap.of("customer_uuid", collector.getCustomerUUID().toString()));
      if (!universeMetadataList.isArray()) {
        return PACollectorExt.InUseStatus.ERROR;
      }
      return universeMetadataList.isEmpty()
          ? PACollectorExt.InUseStatus.NOT_IN_USE
          : PACollectorExt.InUseStatus.IN_USE;
    } catch (Exception e) {
      return PACollectorExt.InUseStatus.ERROR;
    }
  }

  public SupportBundle scheduleSupportBundle(
      PACollector collector,
      UUID universeUuid,
      Instant startTime,
      Instant endTime,
      PrometheusMetricsFormat metricFormat) {
    String scheduleBundleUrl =
        collector.getPaUrl() + "/api/universe/" + universeUuid + "/support_bundle";
    try {
      ImmutableMap.Builder<String, String> queryParamsBuilder =
          ImmutableMap.<String, String>builder()
              .put("startTime", startTime.toString())
              .put("endTime", endTime.toString());

      if (metricFormat != null) {
        queryParamsBuilder.put("metricFormat", metricFormat.name());
      }

      JsonNode result =
          getApiHelper()
              .postWithoutBody(
                  scheduleBundleUrl, authHeader(collector), queryParamsBuilder.build());
      handleError(result);

      return Json.fromJson(result, SupportBundle.class);
    } catch (Exception e) {
      log.error("Failed to schedule support bundle at " + scheduleBundleUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to schedule support bundle at " + scheduleBundleUrl);
    }
  }

  public SupportBundle getSupportBundle(PACollector collector, UUID universeUuid, UUID bundleUuid) {
    String getSupportBundleUrl =
        collector.getPaUrl() + "/api/universe/" + universeUuid + "/support_bundle/" + bundleUuid;
    try {
      JsonNode result = getApiHelper().getRequest(getSupportBundleUrl, authHeader(collector));
      handleError(result);

      return Json.fromJson(result, SupportBundle.class);
    } catch (Exception e) {
      log.error("Failed to get support bundle from " + getSupportBundleUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to get support bundle from " + getSupportBundleUrl);
    }
  }

  public File downloadSupportBundle(
      PACollector collector, UUID universeUuid, UUID bundleUuid, File directory) {
    String downloadUrl =
        collector.getPaUrl()
            + "/api/universe/"
            + universeUuid
            + "/support_bundle/"
            + bundleUuid
            + "/download";
    try {
      return getApiHelper().downloadFile(downloadUrl, authHeader(collector), directory);
    } catch (Exception e) {
      log.error("Failed to download support bundle from " + downloadUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to download support bundle from " + downloadUrl);
    }
  }

  public void deleteSupportBundle(PACollector collector, UUID universeUuid, UUID bundleUuid) {
    String deleteSupportBundleUrl =
        collector.getPaUrl() + "/api/universe/" + universeUuid + "/support_bundle/" + bundleUuid;
    try {
      JsonNode result = getApiHelper().deleteRequest(deleteSupportBundleUrl, authHeader(collector));
      handleError(result);
    } catch (Exception e) {
      log.error("Failed to delete support bundle at " + deleteSupportBundleUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to delete support bundle at " + deleteSupportBundleUrl);
    }
  }

  public Map<String, Long> estimateSupportBundleSize(
      PACollector collector,
      UUID universeUuid,
      Instant startTime,
      Instant endTime,
      PrometheusMetricsFormat metricsFormat) {
    String estimateSizeUrl =
        collector.getPaUrl() + "/api/universe/" + universeUuid + "/support_bundle/estimate_size";
    try {
      Map<String, String> queryParams =
          ImmutableMap.<String, String>builder()
              .put("startTime", startTime.toString())
              .put("endTime", endTime.toString())
              .put("metricFormat", metricsFormat.name())
              .build();

      JsonNode result =
          getApiHelper().getRequest(estimateSizeUrl, authHeader(collector), queryParams);
      handleError(result);

      return Json.mapper().convertValue(result, new TypeReference<>() {});
    } catch (Exception e) {
      log.error("Failed to estimate support bundle size from " + estimateSizeUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to estimate support bundle size from " + estimateSizeUrl);
    }
  }

  private ApiHelper getApiHelper() {
    WSClient wsClient = wsClientRefresher.getClient(WS_CLIENT_KEY);
    return new ApiHelper(wsClient, wsClientRefresher.getMaterializer());
  }

  private Map<String, String> authHeader(PACollector collector) {
    if (StringUtils.isEmpty(collector.getPaApiToken())) {
      return Collections.emptyMap();
    }
    return ImmutableMap.of(TP_API_TOKEN_HEADER, collector.getPaApiToken());
  }

  @Data
  @Accessors(chain = true)
  public static class CustomerMetadata {
    private UUID id;
    private String apiToken;
    String platformUrl;
    String metricsUrl;
    String metricsUsername;
    String metricsPassword;
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

  @Data
  @Accessors(chain = true)
  public static class SupportBundle {

    private UUID id;
    private UUID universeId;
    private SupportBundleState state;
    private PrometheusMetricsFormat metricFormat;

    private String path;
    private Instant scheduledTime;
    private Instant startTime;
    private Instant endTime;

    private String errorMessage;
  }

  public enum SupportBundleState {
    SCHEDULED,
    IN_PROGRESS,
    COMPLETED,
    FAILED
  }

  private void handleError(JsonNode result) {
    if (result.has("status")) {
      int status = result.get("status").asInt();
      String url = result.get("instance").asText();
      JsonNode errors = result.get("properties").get("errors");
      throw new RuntimeException(
          "Request to " + url + " returned status " + status + ": " + errors);
    } else if (result.has("error")) {
      throw new RuntimeException("Request failed: " + result.get("error").asText());
    }
  }
}
