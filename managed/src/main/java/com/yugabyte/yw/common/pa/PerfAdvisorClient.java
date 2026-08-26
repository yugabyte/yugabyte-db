package com.yugabyte.yw.common.pa;

import static play.mvc.Http.Status.BAD_GATEWAY;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.PACollectorExt;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsFormat;
import java.io.File;
import java.time.Instant;
import java.util.ArrayList;
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
  private final RuntimeConfGetter confGetter;

  @Inject
  public PerfAdvisorClient(WSClientRefresher wsClientRefresher, RuntimeConfGetter confGetter) {
    this.wsClientRefresher = wsClientRefresher;
    this.confGetter = confGetter;
  }

  public CustomerMetadata putCustomerMetadata(PACollector collector) {
    String customerMetadataUrl =
        collector.getPaUrl() + "/api/customer/" + collector.getCustomerUUID() + "/metadata";
    try {
      // collection_enabled is derived from local HA state at PUT time - never persisted on
      // PACollector. For non-embedded collectors HA does not apply, so collection is always
      // enabled from YBA's point of view (the operator can still pause collection through
      // the PA UI). For embedded collectors we disable collection when the local YBA is an
      // HA follower.
      boolean collectionEnabled = !(collector.isEmbedded() && HighAvailabilityConfig.isFollower());
      CustomerMetadata customerMetadata =
          new CustomerMetadata()
              .setId(collector.getCustomerUUID())
              .setPlatformUrl(collector.getYbaUrl())
              .setMetricsUrl(collector.getMetricsUrl())
              .setMetricsUsername(collector.getMetricsUsername())
              .setMetricsPassword(collector.getMetricsPassword())
              .setMetricsScrapePeriodSec(collector.getMetricsScrapePeriodSecs())
              .setApiToken(collector.getApiToken())
              .setProxyMode(
                  confGetter.getGlobalConf(GlobalConfKeys.paEmbeddedUiReverseProxyEnabled))
              .setCollectionEnabled(collectionEnabled);
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

  public List<UniverseMetadata> listUniverseMetadata(PACollector collector) {
    String universeMetadataUrl = collector.getPaUrl() + "/api/universe/metadata";
    try {
      JsonNode result =
          getApiHelper()
              .getRequest(
                  universeMetadataUrl,
                  authHeader(collector),
                  ImmutableMap.of("customer_uuid", collector.getCustomerUUID().toString()));
      handleError(result);
      if (!result.isArray()) {
        throw new RuntimeException("Unexpected response received " + result);
      }
      return Json.mapper().convertValue(result, new TypeReference<List<UniverseMetadata>>() {});
    } catch (Exception e) {
      log.error("Failed to list universe metadata from " + universeMetadataUrl, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed to list universe metadata from " + universeMetadataUrl);
    }
  }

  public PACollectorExt.InUseStatus getInUseStatus(PACollector collector) {
    try {
      List<UniverseMetadata> universes = listUniverseMetadata(collector);
      return universes.isEmpty()
          ? PACollectorExt.InUseStatus.NOT_IN_USE
          : PACollectorExt.InUseStatus.IN_USE;
    } catch (Exception e) {
      return PACollectorExt.InUseStatus.ERROR;
    }
  }

  public List<ExportConfig> listExportConfigs(PACollector collector) {
    String url = exportConfigUrl(collector, null);
    JsonNode result = paRequest(getApiHelper().getHttpRequest(url, authHeader(collector), null));
    return Json.mapper().convertValue(result, new TypeReference<List<ExportConfig>>() {});
  }

  public ExportConfig getExportConfig(PACollector collector, UUID configUuid) {
    String url = exportConfigUrl(collector, configUuid);
    JsonNode result = paRequest(getApiHelper().getHttpRequest(url, authHeader(collector), null));
    return Json.fromJson(result, ExportConfig.class);
  }

  public ExportConfig createExportConfig(PACollector collector, ExportConfig config) {
    String url = exportConfigUrl(collector, null);
    JsonNode result =
        paRequest(
            getApiHelper().postHttpRequest(url, Json.toJson(config), authHeader(collector), null));
    return Json.fromJson(result, ExportConfig.class);
  }

  public ExportConfig updateExportConfig(PACollector collector, ExportConfig config) {
    String url = exportConfigUrl(collector, config.getId());
    JsonNode result =
        paRequest(getApiHelper().putHttpRequest(url, Json.toJson(config), authHeader(collector)));
    return Json.fromJson(result, ExportConfig.class);
  }

  public void deleteExportConfig(PACollector collector, UUID configUuid) {
    String url = exportConfigUrl(collector, configUuid);
    paRequest(getApiHelper().deleteHttpRequest(url, authHeader(collector)));
  }

  /**
   * Asks the collector whether it can reach and authenticate against both of an export config's
   * endpoints. Nothing is stored: the collector is only being used as the vantage point, because it
   * is the process that will actually send the data.
   */
  public ExportConfigValidationReport validateExportConfig(
      PACollector collector, ExportConfig config) {
    String url = collector.getPaUrl() + "/api/export/config/validate";
    JsonNode result =
        paRequest(
            getApiHelper().postHttpRequest(url, Json.toJson(config), authHeader(collector), null));
    return Json.fromJson(result, ExportConfigValidationReport.class);
  }

  private static String exportConfigUrl(PACollector collector, UUID configUuid) {
    return collector.getPaUrl()
        + "/api/export/config"
        + (configUuid == null ? "" : "/" + configUuid);
  }

  /**
   * PA writes its failures for an operator to read - an unreachable collection endpoint, a name
   * already taken, the universes still using a config that can't be deleted. Those messages are the
   * whole value of the round trip, so they are passed through with PA's own status rather than
   * collapsed into a 500 the way {@code getBodyOrThrow} would.
   */
  private JsonNode paRequest(ApiHelper.HttpResponse response) {
    JsonNode body = response.getBody();
    if (response.getStatus() >= 200 && response.getStatus() < 300) {
      handleError(body);
      return body;
    }
    String detail = describeErrors(body);
    log.warn(
        "PA request to {} failed with status {}: {}",
        response.getUrl(),
        response.getStatus(),
        detail);
    // A 5xx from PA is not this server failing, so it reaches the caller as a bad gateway.
    int status = response.getStatus() < 500 ? response.getStatus() : BAD_GATEWAY;
    throw new PlatformServiceException(status, detail);
  }

  /**
   * Flattens a Spring {@code ProblemDetail} body into one operator-facing line. Its {@code errors}
   * property is serialized inline, but older PA builds nested it under {@code properties}, and
   * validation failures carry a field name per message while other failures are plain strings.
   */
  private static String describeErrors(JsonNode body) {
    if (body == null || body.isMissingNode()) {
      return "Perf Advisor Collector request failed";
    }
    JsonNode errors = body.get("errors");
    if (errors == null && body.has("properties")) {
      errors = body.get("properties").get("errors");
    }
    if (errors == null || !errors.isArray() || errors.isEmpty()) {
      JsonNode detail = body.get("detail");
      return detail != null ? detail.asText() : body.toString();
    }
    List<String> messages = new ArrayList<>();
    for (JsonNode error : errors) {
      if (error.isTextual()) {
        messages.add(error.asText());
        continue;
      }
      String field = error.path("propertyPath").asText("");
      JsonNode fieldErrors = error.get("errors");
      if (fieldErrors == null || !fieldErrors.isArray()) {
        messages.add(error.toString());
        continue;
      }
      for (JsonNode message : fieldErrors) {
        messages.add(field.isEmpty() ? message.asText() : field + ": " + message.asText());
      }
    }
    return String.join("; ", messages);
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

    /**
     * When true, PA Collector should accept the pre-shared {@code X-AUTH-TP-API-TOKEN} service
     * token as full user-request authentication and skip re-validating any {@code X-AUTH-TOKEN}
     * against YBA. YBA has already authenticated the user and enforced RBAC before proxying the
     * request. See {@code yb.pa.embedded_ui.reverse_proxy.enabled} and {@link
     * com.yugabyte.yw.controllers.PAProxyController}.
     */
    boolean proxyMode;

    /**
     * When false, PA skips scraping, anomaly detection, and task-runner tasks against this
     * customer's universes. YBA sets this to false when the local YBA is an HA follower (embedded
     * collectors only) and back to true after promotion. Operators can also toggle it via the PA UI
     * to pause collection for a specific customer.
     */
    boolean collectionEnabled;
  }

  @Data
  @Accessors(chain = true)
  public static class UniverseMetadata {
    private UUID id;
    private UUID customerId;
    List<String> dataMountPoints;
    List<String> otherMountPoints;
    private boolean metricsExportToPrometheusEnabled = false;
    private boolean metricsExportJsonlEnabled = true;

    /**
     * Every PUT sends the whole record, so an omitted mode would reset the universe to LOCAL on PA.
     * {@link PerfAdvisorService#putUniverse} always sets it explicitly.
     */
    private CollectionMode collectionMode = CollectionMode.LOCAL;

    /** Required and non-empty when {@link #collectionMode} is {@code ONLINE}, unused otherwise. */
    private List<UUID> exportConfigIds;
  }

  /**
   * Where a universe's collected data lives, as PA models it. YBA registers universes in {@code
   * LOCAL} and {@code ONLINE} only - {@code EXTERNAL} belongs to a universe some other Perf Advisor
   * scrapes, and {@code NONE} to one restored from a support bundle.
   */
  public enum CollectionMode {
    LOCAL,
    ONLINE,
    EXTERNAL,
    NONE
  }

  /** An external Perf Advisor that an ONLINE universe's collected data is sent to. */
  @Data
  @Accessors(chain = true)
  public static class ExportConfig {
    private UUID id;
    private String name;
    private String metricsEndpoint;
    private ExportMetricsType metricsType = ExportMetricsType.otlphttp;
    private ExportAuth metricsAuth;
    private String collectionEndpoint;
    private ExportAuth collectionAuth;

    /**
     * Whether the destination also receives Perf Advisor's own metrics, the ones carrying no
     * universe label. Always true from YBA: a BYOC deployment wants the full picture on the
     * receiving side, so this is not exposed as a choice.
     */
    private boolean includeGlobalPaMetrics = true;

    /**
     * The BYOC ingest gateway identifies the sending account and project by header rather than by
     * credential, and needs them on both endpoints. Null for a plain Perf Advisor destination.
     */
    private String ybmAccountId;

    private String ybmProjectId;
  }

  /** One endpoint of an export config, as the collector found it. */
  @Data
  @Accessors(chain = true)
  public static class ExportConfigValidationCheck {
    /** The property the operator has to fix, so a caller can attach the message to an input. */
    private String field;

    private boolean ok;
    private String message;
  }

  /** Both endpoints probed together, so one round trip reports every problem. */
  @Data
  @Accessors(chain = true)
  public static class ExportConfigValidationReport {
    private List<ExportConfigValidationCheck> checks;

    public boolean isValid() {
      return checks == null || checks.stream().allMatch(ExportConfigValidationCheck::isOk);
    }

    public List<ExportConfigValidationCheck> failures() {
      return checks == null ? List.of() : checks.stream().filter(check -> !check.isOk()).toList();
    }
  }

  @Data
  @Accessors(chain = true)
  public static class ExportAuth {
    private ExportAuthType type = ExportAuthType.none;
    private String username;

    /**
     * PA returns this masked and restores the stored value when the mask is echoed back, so it is
     * relayed exactly as received - masking it again here would blank out the credential on every
     * edit.
     */
    private String password;
  }

  public enum ExportMetricsType {
    otlphttp,
    remotewrite
  }

  public enum ExportAuthType {
    none,
    basic
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
