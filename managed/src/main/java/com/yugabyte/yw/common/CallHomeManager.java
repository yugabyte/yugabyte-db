// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.operator.OperatorConfig;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.time.Clock;
import java.time.Duration;
import java.util.Base64;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.stream.Collectors;
import javax.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class CallHomeManager {
  // Used to get software version from yugaware_property table in DB
  ConfigHelper configHelper;

  @Inject private RuntimeConfGetter confGetter;

  // include tasks from a day ago
  private static final Duration CALLHOME_TASK_PERIOD = Duration.ofDays(1);

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

    // k8s operator info
    ObjectNode operatorInfo = Json.newObject();
    operatorInfo.put("enabled", confGetter.getGlobalConf(GlobalConfKeys.KubernetesOperatorEnabled));
    operatorInfo.put("oss_community_mode", OperatorConfig.getOssMode());
    payload.set("k8s_operator", operatorInfo);

    ArrayNode errors = Json.newArray();
    // Build universe details json
    List<UniverseResp> universes =
        c.getUniverses().stream().map(u -> new UniverseResp(u)).collect(Collectors.toList());

    payload.set("universes", Json.toJson(universes));
    // Build provider details json
    ArrayNode providers = Json.newArray();
    for (Provider p : Provider.getAll(c.getUuid())) {
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
}
