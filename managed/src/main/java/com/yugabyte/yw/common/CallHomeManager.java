// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.time.Clock;
import java.util.Base64;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import javax.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

public class CallHomeManager {
  // Used to get software version from yugaware_property table in DB
  ConfigHelper configHelper;

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

  @Inject
  public CallHomeManager(ApiHelper apiHelper, ConfigHelper configHelper) {
    this.apiHelper = apiHelper;
    this.configHelper = configHelper;
  }
  // Email address from YugaByte to which to send diagnostics, if enabled.
  private final String YB_CALLHOME_URL = "https://yw-diagnostics.yugabyte.com";

  public static final Logger LOG = LoggerFactory.getLogger(CallHomeManager.class);

  public void sendDiagnostics(Customer c) {
    CollectionLevel callhomeLevel = CustomerConfig.getOrCreateCallhomeLevel(c.getUuid());
    if (!callhomeLevel.isDisabled()) {
      LOG.info("Starting collecting diagnostics");
      JsonNode payload = CollectDiagnostics(c, callhomeLevel);
      LOG.info("Sending collected diagnostics to " + YB_CALLHOME_URL);
      // Api Helper handles exceptions
      Map<String, String> headers = new HashMap<>();
      headers.put(
          "X-AUTH-TOKEN", Base64.getEncoder().encodeToString(c.getUuid().toString().getBytes()));
      JsonNode response = apiHelper.postRequest(YB_CALLHOME_URL, payload, headers);
      LOG.info("Response: " + response.toString());
    }
  }

  @VisibleForTesting
  JsonNode CollectDiagnostics(Customer c, CollectionLevel callhomeLevel) {
    ObjectNode payload = Json.newObject();
    // Build customer details json
    payload.put("customer_uuid", c.getUuid().toString());
    payload.put("code", c.getCode());
    payload.put("email", Users.getAllEmailsForCustomer(c.getUuid()));
    payload.put("creation_date", c.getCreationDate().toString());
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
      ArrayNode regions = Json.newArray();
      for (Region r : p.getRegions()) {
        regions.add(r.getName());
      }
      provider.set("regions", regions);
      providers.add(provider);
    }
    payload.set("providers", providers);
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
    return payload;
  }
}
