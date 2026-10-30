package com.yugabyte.yw.common.pa;

import static play.mvc.Http.Status.BAD_GATEWAY;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_FOUND;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.troubleshoot.pa.client.v2.ApiClient;
import com.yugabyte.troubleshoot.pa.client.v2.ApiException;
import com.yugabyte.troubleshoot.pa.client.v2.api.CustomerApi;
import com.yugabyte.troubleshoot.pa.client.v2.api.ExportConfigApi;
import com.yugabyte.troubleshoot.pa.client.v2.api.SupportBundleApi;
import com.yugabyte.troubleshoot.pa.client.v2.api.UniverseApi;
import com.yugabyte.troubleshoot.pa.client.v2.model.Credentials;
import com.yugabyte.troubleshoot.pa.client.v2.model.CustomerSpec;
import com.yugabyte.troubleshoot.pa.client.v2.model.Endpoint;
import com.yugabyte.troubleshoot.pa.client.v2.model.ExportConfigMetrics;
import com.yugabyte.troubleshoot.pa.client.v2.model.ExportConfigPagedResp;
import com.yugabyte.troubleshoot.pa.client.v2.model.ExportConfigSpec;
import com.yugabyte.troubleshoot.pa.client.v2.model.ExportConfigValidation;
import com.yugabyte.troubleshoot.pa.client.v2.model.ExportConfigValidationSpec;
import com.yugabyte.troubleshoot.pa.client.v2.model.PageRequest;
import com.yugabyte.troubleshoot.pa.client.v2.model.ScrapedMetricsEndpoint;
import com.yugabyte.troubleshoot.pa.client.v2.model.SupportBundleMetricFormat;
import com.yugabyte.troubleshoot.pa.client.v2.model.SupportBundleSizeEstimate;
import com.yugabyte.troubleshoot.pa.client.v2.model.SupportBundleSpec;
import com.yugabyte.troubleshoot.pa.client.v2.model.TimeWindow;
import com.yugabyte.troubleshoot.pa.client.v2.model.Universe;
import com.yugabyte.troubleshoot.pa.client.v2.model.UniverseMetricsExportSpec;
import com.yugabyte.troubleshoot.pa.client.v2.model.UniversePagedResp;
import com.yugabyte.troubleshoot.pa.client.v2.model.UniverseSpec;
import com.yugabyte.troubleshoot.pa.client.v2.model.YbmProject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.PACollectorExt;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsFormat;
import java.io.File;
import java.io.IOException;
import java.net.http.HttpClient;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.cert.X509Certificate;
import java.time.Duration;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import javax.inject.Singleton;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import lombok.Data;
import lombok.experimental.Accessors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

/** Talks to a Perf Advisor collector through its v2 API, with the generated v2 client. */
@Singleton
@Slf4j
public class PerfAdvisorClient {

  public static final String WS_CLIENT_KEY = "yb.pa.ws";
  public static final String TP_API_TOKEN_HEADER = "X-AUTH-TP-API-TOKEN";
  private static final int PAGE_SIZE = 500;

  // The export config fields a validation check names, from the v2 spec's names to ExportConfig's.
  private static final Map<String, String> EXPORT_CONFIG_FIELDS =
      Map.of(
          "metrics.url", "metricsEndpoint",
          "metrics.protocol", "metricsType",
          "metrics.credentials", "metricsAuth",
          "metrics.include_global_pa_metrics", "includeGlobalPaMetrics",
          "collection.url", "collectionEndpoint",
          "collection.credentials", "collectionAuth",
          "ybm.account_id", "ybmAccountId",
          "ybm.project_id", "ybmProjectId");
  private final RuntimeConfGetter confGetter;

  @Inject
  public PerfAdvisorClient(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  /**
   * The body a PUT would carry for this collector. Exposed so PACollectorSync can build it once,
   * push that same object and remember it: collection_enabled is read from live state here, so a
   * second build could differ from what was sent.
   */
  public CustomerMetadata buildCustomerMetadata(PACollector collector) {
    // collection_enabled is derived from local HA state at PUT time - never persisted on
    // PACollector. For non-embedded collectors HA does not apply, so collection is always
    // enabled from YBA's point of view (the operator can still pause collection through
    // the PA UI). For embedded collectors we disable collection when the local YBA is an
    // HA follower.
    boolean collectionEnabled = !(collector.isEmbedded() && HighAvailabilityConfig.isFollower());
    return new CustomerMetadata()
        .setId(collector.getCustomerUUID())
        .setPlatformUrl(collector.getYbaUrl())
        .setMetricsUrl(collector.getMetricsUrl())
        .setMetricsUsername(collector.getMetricsUsername())
        .setMetricsPassword(collector.getMetricsPassword())
        .setMetricsScrapePeriodSec(collector.getMetricsScrapePeriodSecs())
        .setApiToken(collector.getApiToken())
        .setCollectionEnabled(collectionEnabled);
  }

  public CustomerMetadata putCustomerMetadata(PACollector collector) {
    return putCustomerMetadata(collector, buildCustomerMetadata(collector));
  }

  /** Sends a body the caller has already built. */
  public CustomerMetadata putCustomerMetadata(
      PACollector collector, CustomerMetadata customerMetadata) {
    call(
        collector,
        "register customer " + collector.getCustomerUUID(),
        () ->
            new CustomerApi(apiClient(collector))
                .registerCustomer(collector.getCustomerUUID(), toSpec(customerMetadata)));
    return customerMetadata;
  }

  public void deleteCustomerMetadata(PACollector collector) {
    call(
        collector,
        "delete customer " + collector.getCustomerUUID(),
        () -> {
          new CustomerApi(apiClient(collector)).deleteCustomer(collector.getCustomerUUID());
          return null;
        });
  }

  /** The universe's registration, or null when the collector has none for this customer. */
  public UniverseMetadata getUniverseMetadata(PACollector collector, UUID universeUuid) {
    try {
      Universe universe = new UniverseApi(apiClient(collector)).getUniverse(universeUuid);
      UniverseMetadata metadata = toUniverseMetadata(universe);
      return collector.getCustomerUUID().equals(metadata.getCustomerId()) ? metadata : null;
    } catch (ApiException e) {
      if (e.getCode() == NOT_FOUND) {
        return null;
      }
      throw failure(collector, "get universe " + universeUuid, e);
    }
  }

  public UniverseMetadata putUniverseMetadata(
      PACollector collector, UniverseMetadata universeMetadata) {
    Universe universe =
        call(
            collector,
            "register universe " + universeMetadata.getId(),
            () ->
                new UniverseApi(apiClient(collector))
                    .registerUniverse(universeMetadata.getId(), toSpec(universeMetadata)));
    return toUniverseMetadata(universe);
  }

  public void deleteUniverseMetadata(PACollector collector, UUID universeUuid) {
    call(
        collector,
        "delete universe " + universeUuid,
        () -> {
          new UniverseApi(apiClient(collector)).deleteUniverse(universeUuid, null);
          return null;
        });
  }

  public List<UniverseMetadata> listUniverseMetadata(PACollector collector) {
    UniverseApi api = new UniverseApi(apiClient(collector));
    List<UniverseMetadata> result = new ArrayList<>();
    UniversePagedResp page;
    do {
      int offset = result.size();
      page =
          call(
              collector,
              "list universes",
              () ->
                  api.listUniverses(
                      new PageRequest().offset(offset).limit(PAGE_SIZE),
                      collector.getCustomerUUID(),
                      null,
                      null));
      page.getEntities().forEach(universe -> result.add(toUniverseMetadata(universe)));
    } while (Boolean.TRUE.equals(page.getHasNext()) && !page.getEntities().isEmpty());
    return result;
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
    ExportConfigApi api = new ExportConfigApi(apiClient(collector));
    List<ExportConfig> result = new ArrayList<>();
    ExportConfigPagedResp page;
    do {
      int offset = result.size();
      page =
          call(
              collector,
              "list export configs",
              () -> api.listExportConfigs(new PageRequest().offset(offset).limit(PAGE_SIZE)));
      page.getEntities().forEach(config -> result.add(toExportConfig(config)));
    } while (Boolean.TRUE.equals(page.getHasNext()) && !page.getEntities().isEmpty());
    return result;
  }

  public ExportConfig getExportConfig(PACollector collector, UUID configUuid) {
    return toExportConfig(
        call(
            collector,
            "get export config " + configUuid,
            () -> new ExportConfigApi(apiClient(collector)).getExportConfig(configUuid)));
  }

  public ExportConfig createExportConfig(PACollector collector, ExportConfig config) {
    return toExportConfig(
        call(
            collector,
            "create export config " + config.getName(),
            () -> new ExportConfigApi(apiClient(collector)).createExportConfig(toSpec(config))));
  }

  public ExportConfig updateExportConfig(PACollector collector, ExportConfig config) {
    return toExportConfig(
        call(
            collector,
            "update export config " + config.getId(),
            () ->
                new ExportConfigApi(apiClient(collector))
                    .editExportConfig(config.getId(), toSpec(config))));
  }

  public void deleteExportConfig(PACollector collector, UUID configUuid) {
    call(
        collector,
        "delete export config " + configUuid,
        () -> {
          new ExportConfigApi(apiClient(collector)).deleteExportConfig(configUuid);
          return null;
        });
  }

  /**
   * Asks the collector whether it can reach and authenticate against both of an export config's
   * endpoints. Nothing is stored: the collector is only being used as the vantage point, because it
   * is the process that will actually send the data.
   */
  public ExportConfigValidationReport validateExportConfig(
      PACollector collector, ExportConfig config) {
    ExportConfigValidation validation =
        call(
            collector,
            "validate export config " + config.getName(),
            () ->
                new ExportConfigApi(apiClient(collector))
                    .validateExportConfig(new ExportConfigValidationSpec().spec(toSpec(config))));
    List<ExportConfigValidationCheck> checks = new ArrayList<>();
    if (validation.getChecks() != null) {
      validation
          .getChecks()
          .forEach(
              check ->
                  checks.add(
                      new ExportConfigValidationCheck()
                          .setField(
                              EXPORT_CONFIG_FIELDS.getOrDefault(check.getField(), check.getField()))
                          .setOk(Boolean.TRUE.equals(check.getOk()))
                          .setMessage(check.getMessage())));
    }
    return new ExportConfigValidationReport().setChecks(checks);
  }

  public SupportBundle scheduleSupportBundle(
      PACollector collector,
      UUID universeUuid,
      Instant startTime,
      Instant endTime,
      PrometheusMetricsFormat metricFormat) {
    SupportBundleSpec spec = new SupportBundleSpec().timeWindow(window(startTime, endTime));
    if (metricFormat != null) {
      spec.metricFormat(SupportBundleSpec.MetricFormatEnum.fromValue(metricFormat.name()));
    }
    return toSupportBundle(
        call(
            collector,
            "schedule support bundle for universe " + universeUuid,
            () ->
                new SupportBundleApi(apiClient(collector))
                    .createSupportBundle(universeUuid, spec)));
  }

  public SupportBundle getSupportBundle(PACollector collector, UUID universeUuid, UUID bundleUuid) {
    return toSupportBundle(
        call(
            collector,
            "get support bundle " + bundleUuid,
            () ->
                new SupportBundleApi(apiClient(collector))
                    .getSupportBundle(universeUuid, bundleUuid)));
  }

  public File downloadSupportBundle(
      PACollector collector, UUID universeUuid, UUID bundleUuid, File directory) {
    File downloaded =
        call(
            collector,
            "download support bundle " + bundleUuid,
            () ->
                new SupportBundleApi(apiClient(collector))
                    .downloadSupportBundle(universeUuid, bundleUuid));
    File target = new File(directory, downloaded.getName());
    try {
      Files.move(downloaded.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING);
    } catch (IOException e) {
      throw new PlatformServiceException(
          BAD_GATEWAY, "Failed to store support bundle " + bundleUuid + ": " + e.getMessage());
    }
    return target;
  }

  public void deleteSupportBundle(PACollector collector, UUID universeUuid, UUID bundleUuid) {
    call(
        collector,
        "delete support bundle " + bundleUuid,
        () -> {
          new SupportBundleApi(apiClient(collector))
              .deleteSupportBundle(universeUuid, bundleUuid, null);
          return null;
        });
  }

  /**
   * Estimated sizes by part, {@code metadata} and, when the collector can tell, {@code metrics}.
   */
  public Map<String, Long> estimateSupportBundleSize(
      PACollector collector,
      UUID universeUuid,
      Instant startTime,
      Instant endTime,
      PrometheusMetricsFormat metricsFormat) {
    SupportBundleSizeEstimate estimate =
        call(
            collector,
            "estimate support bundle size for universe " + universeUuid,
            () ->
                new SupportBundleApi(apiClient(collector))
                    .estimateSupportBundleSize(
                        universeUuid,
                        window(startTime, endTime),
                        SupportBundleMetricFormat.fromValue(metricsFormat.name())));
    Map<String, Long> sizes = new HashMap<>();
    sizes.put("metadata", estimate.getMetadataBytes());
    if (estimate.getMetricsBytes() != null) {
      sizes.put("metrics", estimate.getMetricsBytes());
    }
    return sizes;
  }

  private ApiClient apiClient(PACollector collector) {
    HttpClient.Builder http =
        HttpClient.newBuilder()
            .sslContext(acceptAnyCertificate())
            .connectTimeout(timeout("connection"));
    ApiClient client =
        new ApiClient(
            http,
            ApiClient.createDefaultObjectMapper(),
            StringUtils.removeEnd(collector.getPaUrl(), "/") + "/api/v2");
    client.setReadTimeout(timeout("idle"));
    String token = collector.getPaApiToken();
    if (StringUtils.isNotEmpty(token)) {
      client.setRequestInterceptor(request -> request.header(TP_API_TOKEN_HEADER, token));
    }
    return client;
  }

  private Duration timeout(String name) {
    return confGetter.getStaticConf().getDuration(WS_CLIENT_KEY + ".timeout." + name);
  }

  // As yb.pa.ws.ssl.loose.acceptAnyCertificate: a collector's certificate is not validated.
  private static SSLContext acceptAnyCertificate() {
    try {
      SSLContext context = SSLContext.getInstance("TLS");
      context.init(
          null,
          new TrustManager[] {
            new X509TrustManager() {
              @Override
              public void checkClientTrusted(X509Certificate[] chain, String authType) {}

              @Override
              public void checkServerTrusted(X509Certificate[] chain, String authType) {}

              @Override
              public X509Certificate[] getAcceptedIssuers() {
                return new X509Certificate[0];
              }
            }
          },
          null);
      return context;
    } catch (Exception e) {
      throw new IllegalStateException("Failed to set up TLS for Perf Advisor", e);
    }
  }

  private interface Call<T> {
    T run() throws ApiException;
  }

  private <T> T call(PACollector collector, String action, Call<T> call) {
    try {
      return call.run();
    } catch (ApiException e) {
      throw failure(collector, action, e);
    }
  }

  /**
   * PA writes its failures for an operator to read - an unreachable collection endpoint, a name
   * already taken, the universes still using a config that can't be deleted. Those messages are the
   * whole value of the round trip, so they are passed through with PA's own status. A 5xx from PA,
   * or no answer at all, is not this server failing, so it reaches the caller as a bad gateway.
   */
  private static PlatformServiceException failure(
      PACollector collector, String action, ApiException e) {
    String detail =
        e.getCode() == 0
            ? "Failed to reach Perf Advisor Collector at "
                + collector.getPaUrl()
                + ": "
                + e.getMessage()
            : describeErrors(e.getResponseBody());
    log.warn("Perf Advisor request to {} failed with status {}: {}", action, e.getCode(), detail);
    int status = e.getCode() > 0 && e.getCode() < 500 ? e.getCode() : BAD_GATEWAY;
    return new PlatformServiceException(status, detail);
  }

  /** Flattens a v2 error body (RFC 7807 with Perf Advisor's errors) into one line. */
  private static String describeErrors(String responseBody) {
    JsonNode body;
    try {
      body = StringUtils.isEmpty(responseBody) ? null : Json.parse(responseBody);
    } catch (RuntimeException e) {
      return responseBody;
    }
    if (body == null || !body.isObject()) {
      return "Perf Advisor Collector request failed";
    }
    List<String> messages = new ArrayList<>();
    body.path("errors").forEach(error -> messages.add(error.asText()));
    body.path("field_errors")
        .forEach(
            fieldError ->
                fieldError
                    .path("messages")
                    .forEach(
                        message ->
                            messages.add(
                                fieldError.path("field").asText() + ": " + message.asText())));
    if (messages.isEmpty()) {
      return body.path("detail").asText(body.toString());
    }
    return String.join("; ", messages);
  }

  private static CustomerSpec toSpec(CustomerMetadata customer) {
    CustomerSpec spec =
        new CustomerSpec()
            .metrics(
                new ScrapedMetricsEndpoint()
                    .url(customer.getMetricsUrl())
                    .credentials(
                        basic(customer.getMetricsUsername(), customer.getMetricsPassword()))
                    .scrapePeriodSec(customer.getMetricsScrapePeriodSec()))
            .collectionEnabled(customer.isCollectionEnabled());
    if (customer.getPlatformUrl() != null || StringUtils.isNotEmpty(customer.getApiToken())) {
      spec.platform(
          new Endpoint()
              .url(customer.getPlatformUrl())
              .credentials(
                  new Credentials()
                      .type(Credentials.TypeEnum.TOKEN)
                      .token(customer.getApiToken())));
    }
    return spec;
  }

  private static UniverseSpec toSpec(UniverseMetadata universe) {
    return new UniverseSpec()
        .customerUuid(universe.getCustomerId())
        .collectionMode(
            UniverseSpec.CollectionModeEnum.fromValue(universe.getCollectionMode().name()))
        .metricsExport(
            new UniverseMetricsExportSpec()
                .exportConfigIds(
                    universe.getExportConfigIds() == null
                        ? new ArrayList<>()
                        : new ArrayList<>(universe.getExportConfigIds()))
                .prometheusEnabled(universe.isMetricsExportToPrometheusEnabled())
                .jsonlEnabled(universe.isMetricsExportJsonlEnabled()))
        .activeTableMetricsEnabled(universe.isActiveTableMetricsEnabled());
  }

  private static UniverseMetadata toUniverseMetadata(Universe universe) {
    UniverseMetadata metadata = new UniverseMetadata();
    if (universe.getInfo() != null) {
      metadata.setId(universe.getInfo().getUniverseUuid());
    }
    UniverseSpec spec = universe.getSpec();
    if (spec == null) {
      return metadata;
    }
    metadata
        .setCustomerId(spec.getCustomerUuid())
        .setActiveTableMetricsEnabled(Boolean.TRUE.equals(spec.getActiveTableMetricsEnabled()));
    if (spec.getCollectionMode() != null) {
      metadata.setCollectionMode(CollectionMode.valueOf(spec.getCollectionMode().getValue()));
    }
    UniverseMetricsExportSpec export = spec.getMetricsExport();
    if (export != null) {
      metadata
          .setMetricsExportToPrometheusEnabled(Boolean.TRUE.equals(export.getPrometheusEnabled()))
          .setMetricsExportJsonlEnabled(Boolean.TRUE.equals(export.getJsonlEnabled()))
          .setExportConfigIds(export.getExportConfigIds());
    }
    return metadata;
  }

  private static ExportConfigSpec toSpec(ExportConfig config) {
    ExportConfigSpec spec =
        new ExportConfigSpec()
            .name(config.getName())
            .metrics(
                new ExportConfigMetrics()
                    .url(config.getMetricsEndpoint())
                    .protocol(
                        ExportConfigMetrics.ProtocolEnum.fromValue(
                            config.getMetricsType().name().toUpperCase()))
                    .credentials(credentials(config.getMetricsAuth()))
                    .includeGlobalPaMetrics(config.isIncludeGlobalPaMetrics()))
            .collection(
                new Endpoint()
                    .url(config.getCollectionEndpoint())
                    .credentials(credentials(config.getCollectionAuth())));
    if (config.getYbmAccountId() != null || config.getYbmProjectId() != null) {
      spec.ybm(
          new YbmProject()
              .accountId(uuid(config.getYbmAccountId(), "ybm_account_id"))
              .projectId(uuid(config.getYbmProjectId(), "ybm_project_id")));
    }
    return spec;
  }

  private static ExportConfig toExportConfig(
      com.yugabyte.troubleshoot.pa.client.v2.model.ExportConfig config) {
    ExportConfig result = new ExportConfig();
    if (config.getInfo() != null) {
      result.setId(config.getInfo().getExportConfigUuid());
    }
    ExportConfigSpec spec = config.getSpec();
    if (spec == null) {
      return result;
    }
    result.setName(spec.getName());
    if (spec.getMetrics() != null) {
      result
          .setMetricsEndpoint(spec.getMetrics().getUrl())
          .setMetricsAuth(exportAuth(spec.getMetrics().getCredentials()))
          .setIncludeGlobalPaMetrics(
              Boolean.TRUE.equals(spec.getMetrics().getIncludeGlobalPaMetrics()));
      if (spec.getMetrics().getProtocol() != null) {
        result.setMetricsType(
            ExportMetricsType.valueOf(spec.getMetrics().getProtocol().getValue().toLowerCase()));
      }
    }
    if (spec.getCollection() != null) {
      result
          .setCollectionEndpoint(spec.getCollection().getUrl())
          .setCollectionAuth(exportAuth(spec.getCollection().getCredentials()));
    }
    if (spec.getYbm() != null) {
      result
          .setYbmAccountId(string(spec.getYbm().getAccountId()))
          .setYbmProjectId(string(spec.getYbm().getProjectId()));
    }
    return result;
  }

  private static SupportBundle toSupportBundle(
      com.yugabyte.troubleshoot.pa.client.v2.model.SupportBundle bundle) {
    SupportBundle result = new SupportBundle();
    if (bundle.getInfo() != null) {
      result
          .setId(bundle.getInfo().getUuid())
          .setUniverseId(bundle.getInfo().getUniverseUuid())
          .setErrorMessage(bundle.getInfo().getErrorMessage());
      if (bundle.getInfo().getState() != null) {
        result.setState(SupportBundleState.valueOf(bundle.getInfo().getState().getValue()));
      }
      if (bundle.getInfo().getScheduledTime() != null) {
        result.setScheduledTime(bundle.getInfo().getScheduledTime().toInstant());
      }
    }
    if (bundle.getSpec() != null) {
      TimeWindow window = bundle.getSpec().getTimeWindow();
      if (window != null) {
        result
            .setStartTime(window.getStartTime() == null ? null : window.getStartTime().toInstant())
            .setEndTime(window.getEndTime() == null ? null : window.getEndTime().toInstant());
      }
      if (bundle.getSpec().getMetricFormat() != null) {
        result.setMetricFormat(
            PrometheusMetricsFormat.valueOf(bundle.getSpec().getMetricFormat().getValue()));
      }
    }
    return result;
  }

  private static TimeWindow window(Instant start, Instant end) {
    return new TimeWindow()
        .startTime(start == null ? null : OffsetDateTime.ofInstant(start, ZoneOffset.UTC))
        .endTime(end == null ? null : OffsetDateTime.ofInstant(end, ZoneOffset.UTC));
  }

  private static Credentials basic(String username, String password) {
    if (username == null && password == null) {
      return new Credentials().type(Credentials.TypeEnum.NONE);
    }
    return new Credentials().type(Credentials.TypeEnum.BASIC).username(username).password(password);
  }

  private static Credentials credentials(ExportAuth auth) {
    if (auth == null || auth.getType() == ExportAuthType.none) {
      return new Credentials().type(Credentials.TypeEnum.NONE);
    }
    return basic(auth.getUsername(), auth.getPassword());
  }

  private static ExportAuth exportAuth(Credentials credentials) {
    if (credentials == null || credentials.getType() != Credentials.TypeEnum.BASIC) {
      return new ExportAuth();
    }
    return new ExportAuth()
        .setType(ExportAuthType.basic)
        .setUsername(credentials.getUsername())
        .setPassword(credentials.getPassword());
  }

  private static UUID uuid(String value, String field) {
    if (StringUtils.isEmpty(value)) {
      return null;
    }
    try {
      return UUID.fromString(value);
    } catch (IllegalArgumentException e) {
      throw new PlatformServiceException(BAD_REQUEST, field + " must be a UUID, got " + value);
    }
  }

  private static String string(UUID value) {
    return value == null ? null : value.toString();
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
    private boolean metricsExportToPrometheusEnabled = false;
    private boolean metricsExportJsonlEnabled = true;

    /**
     * Whether the PA Collector pushes the universe's active table list to its TServers. Set from
     * {@code yb.pa.active_table_metrics.enabled}; PA Collector versions without the feature ignore
     * it.
     */
    private boolean activeTableMetricsEnabled;

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
}
