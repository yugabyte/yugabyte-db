// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import java.time.Clock;
import java.util.Map;

public class CallHomeManager {

  // Used to get software version from yugaware_property table in DB
  ConfigHelper configHelper = new ConfigHelper();

  // Get timestamp from clock to make testing easier
  Clock clock = Clock.systemUTC();

  ApiHelper apiHelper;

  public CallHomeManager(ApiHelper apiHelper){
    this.apiHelper = apiHelper;
  }
  // Email address from YugaByte to which to send diagnostics, if enabled.
  private final String YB_CALLHOME_URL = "http://yw-diagnostics.yugabyte.com/";

  public static final Logger LOG = LoggerFactory.getLogger(CallHomeManager.class);

  public void sendDiagnostics(Customer c){
    if (!c.getCallHomeLevel().equals("NONE")) {
      LOG.info("Starting collecting diagnostics");
      JsonNode payload = CollectDiagnostics(c);
      LOG.info("Sending collected diagnostics to " + YB_CALLHOME_URL);
      // Api Helper handles exceptions
      JsonNode response = apiHelper.postRequest(YB_CALLHOME_URL, payload);
      LOG.info("Response: " + response.toString());
    }
  }

  @VisibleForTesting
  JsonNode CollectDiagnostics(Customer c) {
    ObjectNode payload = Json.newObject();
    // Build customer details json
    payload.put("customer_uuid", c.uuid.toString());
    payload.put("code", c.code);
    payload.put("email", c.email);
    payload.put("creation_date", c.creationDate.toString());
    // Build universe details json
    ArrayNode universes = Json.newArray();
    for (Universe u : c.getUniverses()) {
      universes.add(u.toJson());
    }
    payload.put("universes", universes);
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
      provider.put("regions", regions);
      providers.add(provider);
    }
    payload.put("providers", providers);
    if (c.getCallHomeLevel().equals("MEDIUM") || c.getCallHomeLevel().equals("HIGH")) {
      // Collect More Stuff
    }
    if (c.getCallHomeLevel().equals("HIGH")) {
      // Collect Even More Stuff
    }
    Map<String, Object> ywMetadata = configHelper.getConfig(ConfigHelper.ConfigType.YugawareMetadata);
    payload.put("yugaware_uuid", ywMetadata.get("yugaware_uuid").toString());
    payload.put("version", ywMetadata.get("version").toString());
    payload.put("timestamp", clock.instant().getEpochSecond());
    return payload;
  }
}
