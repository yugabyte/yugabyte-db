// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.PropertyNamingStrategies;
import com.fasterxml.jackson.databind.annotation.JsonNaming;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.operator.OperatorConfig;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.alerts.AlertChannelService;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.cdc.CdcStream;
import com.yugabyte.yw.common.cdc.CdcStreamManager;
import com.yugabyte.yw.common.config.ConfKeyInfo;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfService;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.CDCReplicationSlotResponse;
import com.yugabyte.yw.forms.MetricQueryParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDestination;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.Data;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class CallHomeManager {
  // Used to get software version from yugaware_property table in DB
  ConfigHelper configHelper;

  @Inject private RuntimeConfGetter confGetter;
  @Inject private RuntimeConfService runtimeConfService;
  @Inject private AlertConfigurationService alertConfigurationService;
  @Inject private AlertService alertService;
  @Inject private AlertChannelService alertChannelService;
  @Inject private AlertDestinationService alertDestinationService;
  @Inject private XClusterUniverseService xClusterUniverseService;
  @Inject private MetricQueryHelper metricQueryHelper;
  @Inject private UniverseTableHandler universeTableHandler;
  @Inject private CdcStreamManager cdcStreamManager;
  // include tasks from a day ago
  private static final Duration CALLHOME_TASK_PERIOD = Duration.ofDays(1);

  // include alerts from a day ago
  private static final Duration CALLHOME_ALERT_PERIOD = Duration.ofDays(1);

  // include metrics from a day ago
  private static final Duration CALLHOME_METRICS_RANGE = Duration.ofDays(1);

  // include yba backup/restore history from a day ago
  private static final Duration CALLHOME_YBA_BACKUP_RANGE = Duration.ofDays(1);

  // Get timestamp from clock to make testing easier
  Clock clock = Clock.systemUTC();

  ApiHelper apiHelper;

  public enum CollectionLevel {
    NONE,
    LOW,
    MEDIUM,
    HIGH;

    public Boolean isDisabled() {
      return toString().equals("NONE");
    }

    public Boolean collectMore() {
      return ordinal() >= MEDIUM.ordinal();
    }

    public Boolean collectAll() {
      return ordinal() == HIGH.ordinal();
    }
  }

  private enum NtpSetupDetail {
    SPECIFY_CUSTOM_NTP_SERVERS,
    USE_CLOUD_NTP_SERVER,
    ASSUME_NTP_CONFIGURED_IN_MACHINE_IMAGE
  }

  private enum CloudCredentialType {
    SPECIFY_ACCESS_KEY,
    USE_INSTANCE_ROLE
  }

  @Data
  @JsonNaming(PropertyNamingStrategies.SnakeCaseStrategy.class)
  public static class UniverseDiagnostics {
    private UUID universeUuid;
    private String healthCheckLastStatus;
    private String healthCheckLastTimestamp;
    private JsonNode healthCheckData;
    private Integer numDatabases;
    private Integer numTables;
    private Map<String, Double> universeMetrics;
    private Integer totalCpuCores;
    private Double totalMemGb;
  }

  @Inject
  public CallHomeManager(ApiHelper apiHelper, ConfigHelper configHelper) {
    this.apiHelper = apiHelper;
    this.configHelper = configHelper;
  }

  // Email address from YugaByte to which to send diagnostics, if enabled.
  private final String YB_CALLHOME_URL = "https://diagnostics.yugabyte.com";

  public static final Logger LOG = LoggerFactory.getLogger(CallHomeManager.class);

  public void sendDiagnostics(Customer c) {
    CollectionLevel callhomeLevel = CustomerConfig.getOrCreateCallhomeLevel(c.getUuid());
    if (!callhomeLevel.isDisabled()) {
      LOG.info("Starting collecting diagnostics");
      JsonNode payload = collectDiagnostics(c, callhomeLevel);
      if (LOG.isTraceEnabled()) {
        LOG.trace(
            "Sending collected diagnostics to {} with payload {}",
            YB_CALLHOME_URL,
            payload.toPrettyString());
      }
      // Api Helper handles exceptions
      Map<String, String> headers = new HashMap<>();
      headers.put(
          "X-AUTH-TOKEN", Base64.getEncoder().encodeToString(c.getUuid().toString().getBytes()));
      JsonNode response = apiHelper.postRequest(YB_CALLHOME_URL, payload, headers);
      LOG.info("Response: {}", response);
    }
  }

  @VisibleForTesting
  public JsonNode collectDiagnostics(Customer c, CollectionLevel callhomeLevel) {
    ObjectNode payload = Json.newObject();
    // Build customer details json
    payload.put("customer_uuid", c.getUuid().toString());
    payload.put("code", c.getCode());
    payload.put("email", Users.getAllEmailDomainsForCustomer(c.getUuid()));
    payload.put("creation_date", c.getCreationDate().toString());

    addAlertMetadata(payload, c);

    // k8s operator info
    ObjectNode operatorInfo = Json.newObject();
    operatorInfo.put("enabled", confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled));
    operatorInfo.put("oss_community_mode", OperatorConfig.getOssMode());
    payload.set("k8s_operator", operatorInfo);

    ArrayNode errors = Json.newArray();
    // Build universe details json
    List<UniverseResp> universes =
        c.getUniverses().stream().map(u -> new UniverseResp(u)).collect(Collectors.toList());
    Set<UUID> universeUuids =
        universes.stream().map(u -> u.universeUUID).collect(Collectors.toSet());

    ArrayNode universesPayload = Json.newArray();
    for (UniverseResp universeResp : universes) {
      ObjectNode universeNode = (ObjectNode) Json.toJson(universeResp);

      List<UUID> sourceConfigUuids =
          universeResp.universeDetails != null && universeResp.universeDetails.delegate != null
              ? universeResp.universeDetails.delegate.xClusterInfo.getSourceXClusterConfigs()
              : Collections.emptyList();

      List<UUID> targetConfigUuids =
          universeResp.universeDetails != null && universeResp.universeDetails.delegate != null
              ? universeResp.universeDetails.delegate.xClusterInfo.getTargetXClusterConfigs()
              : Collections.emptyList();

      universeNode.put(
          "is_xcluster_repl_configured",
          !sourceConfigUuids.isEmpty() || !targetConfigUuids.isEmpty());
      universeNode.put(
          "is_xclusterDR_configured",
          (universeResp.drConfigUuidsAsSource != null
                  && !universeResp.drConfigUuidsAsSource.isEmpty())
              || (universeResp.drConfigUuidsAsTarget != null
                  && !universeResp.drConfigUuidsAsTarget.isEmpty()));
      enrichUniverseXCluster(sourceConfigUuids, targetConfigUuids, universeNode);

      ArrayNode backupPolicies = Json.newArray();
      try {
        for (Schedule schedule : Schedule.getAllSchedulesByOwnerUUID(universeResp.universeUUID)) {
          backupPolicies.add(Json.toJson(schedule));
        }
      } catch (Exception e) {
        backupPolicies = Json.newArray();
      }
      universeNode.set("scheduled_backup_policies", backupPolicies);

      try {
        Universe universe = Universe.getOrBadRequest(universeResp.universeUUID);
        List<CdcStream> cdcStreams = cdcStreamManager.getAllCdcStreams(universe);
        boolean isCdcConfigured = !cdcStreams.isEmpty();
        universeNode.put("is_cdc_configured", isCdcConfigured);

        if (isCdcConfigured) {
          CDCReplicationSlotResponse slotResponse = cdcStreamManager.listReplicationSlot(universe);
          List<String> slotNames =
              slotResponse.replicationSlots.stream()
                  .map(s -> s.slotName)
                  .filter(name -> name != null && !name.isEmpty())
                  .collect(Collectors.toList());
          universeNode.put("cdc_replication_slots", String.join(",", slotNames));
        } else {
          universeNode.put("cdc_replication_slots", "");
        }
      } catch (Exception e) {
        LOG.warn(
            "Failed to fetch CDC info while building callhome payload for universe {}: {}",
            universeResp.universeUUID,
            e.getMessage());
        universeNode.putNull("is_cdc_configured");
        universeNode.putNull("cdc_replication_slots");
      }
      universesPayload.add(universeNode);
    }
    payload.set("universes", universesPayload);
    payload.set("universe_diagnostics", buildUniverseDiagnostics(c, universes));
    // Build provider details json
    ArrayNode providers = Json.newArray();
    Set<UUID> providerUuids = new HashSet<>();
    for (Provider p : Provider.getAll(c.getUuid())) {
      providerUuids.add(p.getUuid());

      ObjectNode provider = Json.newObject();
      provider.put("provider_uuid", p.getUuid().toString());
      provider.put("code", p.getCode());
      provider.put("name", p.getName());

      enrichProviderDiagnostics(provider, p);

      ArrayNode regions = Json.newArray();
      for (Region r : p.getRegions()) {
        regions.add(r.getName());
      }
      provider.set("regions", regions);
      providers.add(provider);
    }
    payload.set("providers", providers);

    // runtime_config json
    payload.set("runtime_config", buildRuntimeConfigPayload(c, providerUuids, universeUuids));

    ArrayNode tasks = Json.newArray();
    List<CustomerTask> customerTasks = CustomerTask.findNewerThan(c, CALLHOME_TASK_PERIOD);

    for (CustomerTask ct : customerTasks) {
      if (ct == null) continue;
      Optional<TaskInfo> optional = TaskInfo.maybeGet(ct.getTaskUUID());
      if (!optional.isPresent()) continue;
      TaskInfo taskInfo = optional.get();
      ObjectNode ctInfo = Json.newObject();
      ctInfo.put("task_name", Objects.toString(taskInfo.getTaskType()));
      ctInfo.put("target_type", Objects.toString(ct.getTargetType()));
      ctInfo.put("target_uuid", Objects.toString(ct.getTargetUUID()));
      ctInfo.put("create_time", Objects.toString(ct.getCreateTime()));
      ctInfo.put("completion_time", Objects.toString(ct.getCompletionTime()));
      ctInfo.put("uuid", Objects.toString(ct.getTaskUUID()));
      ctInfo.put("task_state", Objects.toString(taskInfo.getTaskState()));
      ctInfo.set("task_params", taskInfo.getRedactedParams());
      tasks.add(ctInfo);
    }
    payload.set("tasks", tasks);

    if (callhomeLevel.collectMore()) {
      // Collect More Stuff
    }
    if (callhomeLevel.collectAll()) {
      // Collect Even More Stuff
    }
    Map<String, Object> ywMetadata =
        configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata);
    if (ywMetadata.get("yugaware_uuid") != null) {
      payload.put("yugaware_uuid", ywMetadata.get("yugaware_uuid").toString());
    }
    if (ywMetadata.get("version") != null) {
      payload.put("version", ywMetadata.get("version").toString());
    }
    payload.put("timestamp", clock.instant().getEpochSecond());
    payload.set("errors", errors);
    return RedactingService.filterSecretFields(payload, RedactingService.RedactionTarget.LOGS);
  }

  private void enrichProviderDiagnostics(ObjectNode provider, Provider p) {
    ProviderDetails details = p.getDetails();
    String providerCode = p.getCode();

    provider.put("is_airgap", details.airGapInstall);
    if (providerCode.equals("onprem")) {
      provider.put("on_prem_manually_provision", details.skipProvisioning);
    } else {
      provider.putNull("on_prem_manually_provision");
    }

    boolean isPublicCloud =
        providerCode.equals("aws") || providerCode.equals("gcp") || providerCode.equals("azu");
    boolean hasCustomNtpServers = details.ntpServers != null && !details.ntpServers.isEmpty();

    NtpSetupDetail ntpSetupDetail;
    if (!details.setUpChrony) {
      ntpSetupDetail = NtpSetupDetail.ASSUME_NTP_CONFIGURED_IN_MACHINE_IMAGE;
    } else if (hasCustomNtpServers) {
      ntpSetupDetail = NtpSetupDetail.SPECIFY_CUSTOM_NTP_SERVERS;
    } else if (isPublicCloud) {
      ntpSetupDetail = NtpSetupDetail.USE_CLOUD_NTP_SERVER;
    } else {
      ntpSetupDetail = NtpSetupDetail.ASSUME_NTP_CONFIGURED_IN_MACHINE_IMAGE;
    }

    provider.put("ntp_setup_detail", ntpSetupDetail.toString());
    provider.putNull("public_cloud_credential_type");
    ProviderDetails.CloudInfo cloudInfo = details.getCloudInfo();
    if (cloudInfo != null) {

      Boolean useAccessKey = null;

      if (providerCode.equals("aws") && cloudInfo.getAws() != null) {
        useAccessKey =
            cloudInfo.getAws().awsAccessKeyID != null
                && !cloudInfo.getAws().awsAccessKeyID.isEmpty();

      } else if (providerCode.equals("gcp") && cloudInfo.getGcp() != null) {
        Boolean useHostCreds = cloudInfo.getGcp().getUseHostCredentials();
        useAccessKey = !(useHostCreds != null && useHostCreds);

      } else if (providerCode.equals("azu") && cloudInfo.getAzu() != null) {
        useAccessKey =
            cloudInfo.getAzu().azuClientSecret != null
                && !cloudInfo.getAzu().azuClientSecret.isEmpty();
      }

      if (useAccessKey != null) {
        CloudCredentialType type =
            useAccessKey
                ? CloudCredentialType.SPECIFY_ACCESS_KEY
                : CloudCredentialType.USE_INSTANCE_ROLE;

        provider.put("public_cloud_credential_type", type.toString());
      }
    }
  }

  private ArrayNode buildRuntimeConfigPayload(
      Customer c, Set<UUID> providerUuids, Set<UUID> universeUuids) {
    Set<UUID> scopeUuids = new HashSet<>(providerUuids);
    scopeUuids.addAll(universeUuids);
    scopeUuids.add(c.getUuid());
    scopeUuids.add(ScopedRuntimeConfig.GLOBAL_SCOPE_UUID);

    List<RuntimeConfigEntry> allEntries = runtimeConfService.getRuntimeConfigEntries(scopeUuids);

    Set<String> customerOverriddenPaths =
        allEntries.stream()
            .filter(e -> c.getUuid().equals(e.getScopeUUID()))
            .map(RuntimeConfigEntry::getPath)
            .collect(Collectors.toSet());

    List<RuntimeConfigEntry> filteredEntries =
        allEntries.stream()
            .filter(
                e ->
                    !(ScopedRuntimeConfig.GLOBAL_SCOPE_UUID.equals(e.getScopeUUID())
                        && customerOverriddenPaths.contains(e.getPath())))
            .collect(Collectors.toList());

    ArrayNode runtimeConfigEntries = (ArrayNode) Json.toJson(filteredEntries);

    for (int i = 0; i < runtimeConfigEntries.size(); i++) {
      ObjectNode obj = (ObjectNode) runtimeConfigEntries.get(i);

      UUID scopeUuid = UUID.fromString(obj.path("scopeUUID").asText());

      String scopeType;
      if (ScopedRuntimeConfig.GLOBAL_SCOPE_UUID.equals(scopeUuid)) {
        scopeType = "GLOBAL";
      } else if (c.getUuid().equals(scopeUuid)) {
        scopeType = "CUSTOMER";
      } else if (providerUuids.contains(scopeUuid)) {
        scopeType = "PROVIDER";
      } else {
        scopeType = "UNIVERSE";
      }
      obj.put("scope_type", scopeType);

      String path = obj.path("path").asText();
      if (CommonUtils.isSensitiveField(path)) {
        obj.put("value", RedactingService.SECRET_REPLACEMENT);
      }
    }

    return runtimeConfigEntries;
  }

  private void addAlertMetadata(ObjectNode payload, Customer c) {
    List<AlertConfiguration> alertConfigurations =
        alertConfigurationService.listByCustomerUuid(c.getUuid());
    payload.set("alert_policies", Json.toJson(alertConfigurations));

    List<AlertDestination> alertDestinations = alertDestinationService.listByCustomer(c.getUuid());
    payload.set("alert_destinations", Json.toJson(alertDestinations));

    ArrayNode allChannels = Json.newArray();
    for (AlertChannel ch : alertChannelService.list(c.getUuid())) {
      ObjectNode one = Json.newObject();
      one.put("uuid", ch.getUuid().toString());
      one.put("type", ch.getParams().getChannelType().toString());
      one.put("name", ch.getName());
      allChannels.add(one);
    }
    payload.set("alert_notification_channels", allChannels);

    Instant cutoff = clock.instant().minus(CALLHOME_ALERT_PERIOD);
    List<Alert> alerts = alertService.listByCustomerSince(c.getUuid(), Date.from(cutoff));
    payload.set("alert_list", Json.toJson(alerts));
  }

  private void enrichUniverseXCluster(
      List<UUID> sourceConfigUuids, List<UUID> targetConfigUuids, ObjectNode universeNode) {

    Set<UUID> allUuids = new HashSet<>();
    allUuids.addAll(sourceConfigUuids);
    allUuids.addAll(targetConfigUuids);

    Map<UUID, XClusterConfig> configMap =
        xClusterUniverseService.getXClusterConfigsByUuids(allUuids);

    ArrayNode asSource = Json.newArray();
    ArrayNode asTarget = Json.newArray();

    for (UUID configUuid : sourceConfigUuids) {
      XClusterConfig config = configMap.get(configUuid);
      if (config != null) {
        ObjectNode item = (ObjectNode) Json.toJson(config);
        config.maybeGetDrConfig().ifPresent(dr -> item.set("dr_config", Json.toJson(dr)));
        asSource.add(item);
      }
    }

    for (UUID configUuid : targetConfigUuids) {
      XClusterConfig config = configMap.get(configUuid);
      if (config != null) {
        ObjectNode item = (ObjectNode) Json.toJson(config);
        config.maybeGetDrConfig().ifPresent(dr -> item.set("dr_config", Json.toJson(dr)));
        asTarget.add(item);
      }
    }

    ObjectNode xclusterSettings = Json.newObject();
    xclusterSettings.set("asSource", asSource);
    xclusterSettings.set("asTarget", asTarget);
    universeNode.set("xclusterSettings", xclusterSettings);
  }

  private ArrayNode buildUniverseDiagnostics(Customer c, List<UniverseResp> universes) {
    ArrayNode arr = Json.newArray();
    for (UniverseResp universeResp : universes) {
      UniverseDiagnostics diag = new UniverseDiagnostics();
      diag.setUniverseUuid(universeResp.universeUUID);

      String nodePrefix = universeResp.universeDetails.delegate.nodePrefix;

      HealthCheck latest = HealthCheck.getLatest(universeResp.universeUUID);
      if (latest == null) {
        diag.setHealthCheckLastStatus("UNKNOWN");
      } else {
        HealthCheck.Details details = latest.getDetailsJson();
        diag.setHealthCheckLastTimestamp(latest.getIdKey().checkTime.toInstant().toString());
        if (details.getHasError()) {
          diag.setHealthCheckLastStatus("ERROR");
        } else if (details.getHasWarning()) {
          diag.setHealthCheckLastStatus("WARNING");
        } else {
          diag.setHealthCheckLastStatus("OK");
        }
        if (details.getData() != null) {
          diag.setHealthCheckData(Json.toJson(details.getData()));
        }
      }
      boolean isK8s =
          universeResp.universeDetails.delegate.getPrimaryCluster().userIntent.providerType
              == CloudType.kubernetes;
      Map<String, Double> metrics = getUniverseMetrics(c, nodePrefix, isK8s, clock.instant());
      diag.setUniverseMetrics(metrics);

      int totalCores = 0;
      double totalMemGb = 0.0;
      for (UniverseDefinitionTaskParams.Cluster cluster :
          universeResp.universeDetails.delegate.clusters) {
        UniverseDefinitionTaskParams.UserIntent intent = cluster.userIntent;
        InstanceType it = InstanceType.get(UUID.fromString(intent.provider), intent.instanceType);
        if (it != null) {
          if (it.getNumCores() != null)
            totalCores += (int) Math.round(it.getNumCores() * intent.numNodes);
          if (it.getMemSizeGB() != null) totalMemGb += it.getMemSizeGB() * intent.numNodes;
        }
      }
      diag.setTotalCpuCores(totalCores > 0 ? totalCores : null);
      diag.setTotalMemGb(totalMemGb > 0 ? totalMemGb : null);

      try {
        int tableCount =
            universeTableHandler
                .listTables(c.getUuid(), universeResp.universeUUID, false, false, false, false)
                .size();

        int dbCount =
            universeTableHandler
                .listNamespaces(c.getUuid(), universeResp.universeUUID, false)
                .size();

        diag.setNumTables(tableCount);
        diag.setNumDatabases(dbCount);
      } catch (Exception e) {
      }

      arr.add(Json.toJson(diag));
    }
    return arr;
  }

  private Map<String, Double> getUniverseMetrics(
      Customer customer, String nodePrefix, boolean isK8s, Instant now) {

    MetricQueryParams params = new MetricQueryParams();
    params.setNodePrefix(nodePrefix);
    params.setStart(now.getEpochSecond());
    params.setEnd(now.getEpochSecond());
    params.setStep(CALLHOME_METRICS_RANGE.getSeconds());
    params.setMetrics(
        isK8s
            ? List.of(
                "container_cpu_usage",
                "container_memory_usage",
                "container_volume_stats",
                "container_volume_max_usage",
                "ysql_server_rpc_per_second",
                "cql_server_rpc_per_second")
            : List.of(
                "cpu_usage",
                "disk_usage",
                "memory_usage",
                "ysql_server_rpc_per_second",
                "cql_server_rpc_per_second"));

    Map<String, Double> result = new HashMap<>();
    try {
      JsonNode response = metricQueryHelper.query(customer, params);
      for (JsonNode d : response.path(isK8s ? "container_cpu_usage" : "cpu_usage").path("data")) {
        JsonNode y = d.path("y");
        if (y.isArray() && y.size() > 0) {
          String name = isK8s ? "total" : d.path("name").asText().toLowerCase();
          result.put("cpu_" + name, Double.parseDouble(y.get(0).asText()));
        }
      }
      double usedGb = 0, sizeGb = 0;
      if (isK8s) {
        for (JsonNode d : response.path("container_volume_stats").path("data")) {
          usedGb = Double.parseDouble(d.path("y").get(0).asText());
        }
        for (JsonNode d : response.path("container_volume_max_usage").path("data")) {
          sizeGb = Double.parseDouble(d.path("y").get(0).asText());
        }
      } else {
        double freeGb = 0;
        for (JsonNode d : response.path("disk_usage").path("data")) {
          double val = Double.parseDouble(d.path("y").get(0).asText());
          if ("free".equals(d.path("name").asText())) freeGb = val;
          if ("size".equals(d.path("name").asText())) sizeGb = val;
        }
        usedGb = sizeGb - freeGb;
      }
      if (sizeGb > 0) {
        result.put("disk_used_gb", usedGb);
        result.put("disk_total_gb", sizeGb);
        result.put("disk_usage_percent", (usedGb / sizeGb) * 100.0);
      }

      if (isK8s) {
        JsonNode data = response.path("container_memory_usage").path("data");
        if (data.size() > 0) {
          result.put("memory_used_gb", Double.parseDouble(data.get(0).path("y").get(0).asText()));
        }
      } else {
        for (JsonNode d : response.path("memory_usage").path("data")) {
          double val = Double.parseDouble(d.path("y").get(0).asText());
          String name = d.path("name").asText().toLowerCase();
          result.put("memory_" + name + "_gb", val);
        }
      }

      double ysqlRead = 0.0;
      double ysqlWrite = 0.0;
      for (JsonNode d : response.path("ysql_server_rpc_per_second").path("data")) {
        double val = Double.parseDouble(d.path("y").get(0).asText());
        if ("Select".equals(d.path("name").asText())) {
          ysqlRead = val;
        } else {
          ysqlWrite += val;
        }
      }
      result.put("ysql_read_ops", ysqlRead);
      result.put("ysql_write_ops", ysqlWrite);

      double ycqlRead = 0.0;
      double ycqlWrite = 0.0;
      for (JsonNode d : response.path("cql_server_rpc_per_second").path("data")) {
        double val = Double.parseDouble(d.path("y").get(0).asText());
        if ("Select".equals(d.path("name").asText())) {
          ycqlRead = val;
        } else {
          ycqlWrite += val;
        }
      }
      result.put("ycql_read_ops", ycqlRead);
      result.put("ycql_write_ops", ycqlWrite);

    } catch (Exception ex) {
    }
    return result;
  }

  public void sendPlatformDiagnostics() {
    boolean anyEnabled =
        Customer.getAll().stream()
            .anyMatch(c -> !CustomerConfig.getOrCreateCallhomeLevel(c.getUuid()).isDisabled());
    if (!anyEnabled) {
      return;
    }
    JsonNode payload = collectPlatformDiagnostics();
    if (LOG.isTraceEnabled()) {
      LOG.trace(
          "Sending platform diagnostics to {} with payload {}",
          YB_CALLHOME_URL,
          payload.toPrettyString());
    }
    JsonNode response = apiHelper.postRequest(YB_CALLHOME_URL, payload, null);
    LOG.info("Platform callhome response: {}", response);
  }

  @VisibleForTesting
  public JsonNode collectPlatformDiagnostics() {
    ObjectNode payload = Json.newObject();
    payload.put("payload_type", "platform");

    Map<String, Object> ywMetadata =
        configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata);
    UUID yugawareUuid = null;
    if (ywMetadata.get("yugaware_uuid") != null) {
      String uuidStr = ywMetadata.get("yugaware_uuid").toString();
      payload.put("yugaware_uuid", uuidStr);
      yugawareUuid = UUID.fromString(uuidStr);
    }
    if (ywMetadata.get("version") != null) {
      payload.put("yba_version", ywMetadata.get("version").toString());
    }
    Instant now = clock.instant();
    payload.put("timestamp", now.getEpochSecond());

    ArrayNode customerUuids = Json.newArray();
    Customer.getAll().forEach(c -> customerUuids.add(c.getUuid().toString()));
    payload.set("customer_uuids", customerUuids);

    Optional<HighAvailabilityConfig> haConfigOpt = HighAvailabilityConfig.get();
    payload.put("is_yba_ha_enabled", haConfigOpt.isPresent());
    ArrayNode standbyHostnames = Json.newArray();
    haConfigOpt.ifPresent(
        ha -> ha.getRemoteInstances().forEach(i -> standbyHostnames.add(i.getAddress())));
    payload.set("hostname_of_standby_yba_ha_instances", standbyHostnames);

    ArrayNode backupHistory = Json.newArray();
    ArrayNode restoreHistory = Json.newArray();
    if (yugawareUuid != null) {
      Date since = new Date(now.minus(CALLHOME_YBA_BACKUP_RANGE).toEpochMilli());
      for (CustomerTask ct :
          CustomerTask.findByTargetUUIDsAndTypesSince(
              Arrays.asList(yugawareUuid, Util.NULL_UUID),
              CustomerTask.TargetType.Yba,
              Arrays.asList(CustomerTask.TaskType.CreateYbaBackup),
              since)) {
        ObjectNode entry = (ObjectNode) Json.toJson(ct);
        if (ct.getTaskInfo() != null && ct.getTaskInfo().getTaskState() != null) {
          entry.put("task_state", ct.getTaskInfo().getTaskState().toString());
        }
        ScheduleTask scheduleTask = ScheduleTask.fetchByTaskUUID(ct.getTaskUUID());
        boolean isScheduled = scheduleTask != null;
        entry.put("is_scheduled", isScheduled);
        if (isScheduled && scheduleTask.getScheduleUUID() != null) {
          entry.put("scheduled_backup_policy_uuid", scheduleTask.getScheduleUUID().toString());
        } else {
          entry.putNull("scheduled_backup_policy_uuid");
        }
        backupHistory.add(entry);
      }

      for (CustomerTask ct :
          CustomerTask.findByTargetUUIDsAndTypesSince(
              Arrays.asList(yugawareUuid),
              CustomerTask.TargetType.Yba,
              Arrays.asList(
                  CustomerTask.TaskType.RestoreYbaBackup,
                  CustomerTask.TaskType.RestoreContinuousBackup),
              since)) {
        ObjectNode entry = (ObjectNode) Json.toJson(ct);
        if (ct.getTaskInfo() != null && ct.getTaskInfo().getTaskState() != null) {
          entry.put("task_state", ct.getTaskInfo().getTaskState().toString());
        }
        restoreHistory.add(entry);
      }
    }
    payload.set("yba_backup_history", backupHistory);
    payload.set("yba_restore_history", restoreHistory);
    List<ConfKeyInfo<?>> oidcKeys =
        Arrays.asList(
            GlobalConfKeys.useOauth,
            GlobalConfKeys.ybSecurityType,
            GlobalConfKeys.ybClientID,
            GlobalConfKeys.discoveryURI,
            GlobalConfKeys.oidcScope,
            GlobalConfKeys.oidcEmailAttribute);

    List<ConfKeyInfo<?>> ldapKeys =
        Arrays.asList(
            GlobalConfKeys.useLdap,
            GlobalConfKeys.ldapUrl,
            GlobalConfKeys.ldapPort,
            GlobalConfKeys.ldapBaseDn,
            GlobalConfKeys.ldapDnPrefix,
            GlobalConfKeys.ldapServiceAccountDistinguishedName,
            GlobalConfKeys.enableLdap,
            GlobalConfKeys.enableLdapStartTls,
            GlobalConfKeys.ldapUseSearchAndBind,
            GlobalConfKeys.ldapSearchAttribute,
            GlobalConfKeys.ldapGroupSearchFilter,
            GlobalConfKeys.ldapGroupSearchScope,
            GlobalConfKeys.ldapGroupSearchBaseDn,
            GlobalConfKeys.ldapGroupMemberOfAttribute,
            GlobalConfKeys.ldapGroupUseQuery,
            GlobalConfKeys.ldapGroupUseRoleMapping,
            GlobalConfKeys.ldapDefaultRole,
            GlobalConfKeys.ldapTlsProtocol,
            GlobalConfKeys.ldapsEnforceCertVerification,
            GlobalConfKeys.ldapPageQuerySize);

    List<ConfKeyInfo<?>> globalKeys = new ArrayList<>(oidcKeys);
    globalKeys.addAll(ldapKeys);

    Map<String, Object> globalValues = confGetter.getGlobalConfValues(globalKeys);

    boolean useOauth = Boolean.TRUE.equals(globalValues.get(GlobalConfKeys.useOauth.getKey()));
    String securityType =
        Objects.toString(globalValues.get(GlobalConfKeys.ybSecurityType.getKey()), "");
    boolean useLdap = Boolean.TRUE.equals(globalValues.get(GlobalConfKeys.useLdap.getKey()));
    boolean isOidcEnabled = useOauth && "OIDC".equalsIgnoreCase(securityType);
    payload.put("is_user_auth_via_oidc_configured", isOidcEnabled);
    payload.put("is_user_auth_via_ldap", useLdap);

    if (isOidcEnabled) {
      ObjectNode oidcInfo = Json.newObject();
      for (ConfKeyInfo<?> keyInfo : oidcKeys) {
        String key = keyInfo.getKey();
        if (key.equals(GlobalConfKeys.useOauth.getKey())
            || key.equals(GlobalConfKeys.ybSecurityType.getKey())) {
          continue;
        }
        oidcInfo.set(key, Json.toJson(globalValues.get(key)));
      }
      payload.set("oidc_config", oidcInfo);
    } else {
      payload.putNull("oidc_config");
    }

    if (useLdap) {
      ObjectNode ldapConfig = Json.newObject();
      for (ConfKeyInfo<?> keyInfo : ldapKeys) {
        String key = keyInfo.getKey();
        if (key.equals(GlobalConfKeys.useLdap.getKey())) {
          continue;
        }
        ldapConfig.set(key, Json.toJson(globalValues.get(key)));
      }
      payload.set("ldap_config", ldapConfig);
    } else {
      payload.putNull("ldap_config");
    }
    return RedactingService.filterSecretFields(payload, RedactingService.RedactionTarget.LOGS);
  }
}
