// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import org.asynchttpclient.util.Base64;

import com.yugabyte.yw.models.CustomerConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.inject.Inject;
import java.time.Clock;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

public class CallHomeManager {
  // Used to get software version from yugaware_property table in DB
  ConfigHelper configHelper;

  // Get timestamp from clock to make testing easier
  Clock clock = Clock.systemUTC();

  ApiHelper apiHelper;

  public enum CollectionLevel{
    NONE,
    LOW,
    MEDIUM,
    HIGH;

    public Boolean isDisabled(){
      return toString().equals("NONE");
    }

    public Boolean collectMore(){
      return ordinal() >= MEDIUM.ordinal();
    }

    public Boolean collectAll(){
      return ordinal() == HIGH.ordinal();
    }

  }

  @Inject
  public CallHomeManager(ApiHelper apiHelper, ConfigHelper configHelper){
    this.apiHelper = apiHelper;
    this.configHelper = configHelper;
  }
  // Email address from YugaByte to which to send diagnostics, if enabled.
  private final String YB_CALLHOME_URL = "http://yw-diagnostics.yugabyte.com";

  public static final Logger LOG = LoggerFactory.getLogger(CallHomeManager.class);

  public void sendDiagnostics(Customer c){
    CollectionLevel callhomeLevel = CustomerConfig.getOrCreateCallhomeLevel(c.uuid);
    if (!callhomeLevel.isDisabled()) {
      LOG.info("Starting collecting diagnostics");
      JsonNode payload = CollectDiagnostics(c, callhomeLevel);
      LOG.info("Sending collected diagnostics to " + YB_CALLHOME_URL);
      // Api Helper handles exceptions
      Map<String, String> headers = new HashMap<>();
      headers.put("X-AUTH-TOKEN", Base64.encode(c.uuid.toString().getBytes()));
      JsonNode response = apiHelper.postRequest(YB_CALLHOME_URL, payload, headers);
      LOG.info("Response: " + response.toString());
    }
  }

  @VisibleForTesting
  JsonNode CollectDiagnostics(Customer c, CollectionLevel callhomeLevel) {
    ObjectNode payload = Json.newObject();
    // Build customer details json
    payload.put("customer_uuid", c.uuid.toString());
    payload.put("code", c.code);
    payload.put("email", Users.getAllEmailsForCustomer(c.uuid));
    payload.put("creation_date", c.creationDate.toString());
    ArrayNode errors = Json.newArray();

    // Build universe details json
    ArrayNode universes = Json.newArray();
    for (UUID universeUUID : c.getUniverseUUIDs()) {
      try {
        Universe u = Universe.getOrBadRequest(universeUUID);
        universes.add(u.toJson());
      } catch (RuntimeException re) {
        errors.add(re.getMessage());
      }
    }

    payload.set("universes", universes);
    // Build provider details json
    ArrayNode providers = Json.newArray();
    for (Provider p : Provider.getAll(c.uuid)) {
      ObjectNode provider = Json.newObject();
      provider.put("provider_uuid", p.uuid.toString());
      provider.put("code", p.code);
      provider.put("name", p.name);
      ArrayNode regions = Json.newArray();
      for (Region r : p.regions) {
        regions.add(r.name);
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
    Map<String, Object> ywMetadata = configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata);
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
