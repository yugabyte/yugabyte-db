// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registerOk;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registrationSpec;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.util.Optional;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;

public class HostCloudInfoApiTest extends FakeDBApplication {

  private UUID customerUuid;
  private String apiToken;

  @Before
  public void registerSuperAdmin() {
    JsonNode registration = registerOk(app, registrationSpec("admin@example.com"), null);
    customerUuid = UUID.fromString(registration.get("customer_uuid").asText());
    apiToken = registration.get("api_token").asText();
  }

  private Result get(UUID cUUID, String token) {
    return Helpers.route(
        app,
        Helpers.fakeRequest("GET", "/api/v2/customers/" + cUUID + "/host-cloud-info")
            .header(TokenAuthenticator.API_TOKEN_HEADER, token));
  }

  private JsonNode getOk() {
    Result result = get(customerUuid, apiToken);
    assertEquals(contentAsString(result), OK, result.status());
    return Json.parse(contentAsString(result));
  }

  private void hostOn(CloudType cloud, String ybopsOutput) {
    when(mockHostCloudDetector.getHostCloud()).thenReturn(Optional.of(cloud));
    when(mockCloudQueryHelper.getCurrentHostInfo(cloud)).thenReturn(Json.parse(ybopsOutput));
  }

  @Test
  public void aws() {
    hostOn(
        CloudType.aws,
        "{\"instance-id\": \"i-0abc\", \"vpc-id\": \"vpc-123\", \"privateIp\": \"10.0.0.5\","
            + " \"region\": \"us-west-2\"}");

    JsonNode info = getOk();

    assertEquals("aws", info.get("cloud").asText());
    assertEquals("us-west-2", info.get("region").asText());
    assertEquals(
        Json.parse(
            "{\"instance_id\": \"i-0abc\", \"vpc_id\": \"vpc-123\", \"private_ip\": \"10.0.0.5\","
                + " \"region\": \"us-west-2\"}"),
        info.get("aws"));
    assertFalse(info.has("gcp"));
    assertFalse(info.has("azu"));
    // Reads aren't audited.
    assertAuditEntry(1, customerUuid);
  }

  @Test
  public void gcp() {
    hostOn(
        CloudType.gcp,
        "{\"network\": \"yb-vpc\", \"shared_vpc_project\": \"123456\", \"project\": \"yb-proj\","
            + " \"zone\": \"us-central1-a\", \"region\": \"us-central1\"}");

    JsonNode info = getOk();

    assertEquals("gcp", info.get("cloud").asText());
    assertEquals("us-central1", info.get("region").asText());
    assertEquals(
        Json.parse(
            "{\"project\": \"yb-proj\", \"network\": \"yb-vpc\", \"shared_vpc_project\":"
                + " \"123456\", \"zone\": \"us-central1-a\", \"region\": \"us-central1\"}"),
        info.get("gcp"));
  }

  @Test
  public void azure() {
    hostOn(
        CloudType.azu,
        "{\"compute\": {\"location\": \"eastus\", \"subscriptionId\": \"sub-1\","
            + " \"resourceGroupName\": \"yb-rg\", \"vmId\": \"vm-1\"},"
            + " \"network\": {\"interface\": [{\"ipv4\": {\"ipAddress\":"
            + " [{\"privateIpAddress\": \"10.1.0.4\", \"publicIpAddress\": \"\"}]}}]}}");

    JsonNode info = getOk();

    assertEquals("azu", info.get("cloud").asText());
    assertEquals("eastus", info.get("region").asText());
    assertEquals(
        Json.parse(
            "{\"subscription_id\": \"sub-1\", \"resource_group\": \"yb-rg\", \"vm_id\": \"vm-1\","
                + " \"private_ip\": \"10.1.0.4\", \"region\": \"eastus\"}"),
        info.get("azu"));
  }

  @Test
  public void noCloud() {
    when(mockHostCloudDetector.getHostCloud()).thenReturn(Optional.empty());

    assertEquals(Json.newObject(), getOk());
    verify(mockCloudQueryHelper, never()).getCurrentHostInfo(any());
  }

  @Test
  public void cloudWithoutMetadata() {
    // What ybops prints when the metadata service is unreachable.
    hostOn(CloudType.aws, "\"Unable to auto-discover AWS provider information\"");

    assertEquals(Json.parse("{\"cloud\": \"aws\"}"), getOk());
  }

  @Test
  public void cloudWithFailedYbops() {
    hostOn(CloudType.gcp, "{\"error\": \"YBCloud command query (current-host) failed\"}");

    assertEquals(Json.parse("{\"cloud\": \"gcp\"}"), getOk());
  }

  @Test
  public void noTokenIsUnauthorized() {
    Result result =
        Helpers.route(
            app,
            Helpers.fakeRequest("GET", "/api/v2/customers/" + customerUuid + "/host-cloud-info"));

    assertEquals(contentAsString(result), UNAUTHORIZED, result.status());
  }

  @Test
  public void otherOrUnknownCustomerIsUnauthorized() {
    Users other = ModelFactory.testUser(ModelFactory.testCustomer("other", "Other Customer"));
    Result othersToken = get(customerUuid, other.upsertApiToken());
    Result unknownCustomer = get(UUID.randomUUID(), apiToken);

    // As on every customer-scoped endpoint, authorization rejects a customer other than the
    // caller's before it is looked up.
    assertEquals(contentAsString(othersToken), UNAUTHORIZED, othersToken.status());
    assertEquals(contentAsString(unknownCustomer), UNAUTHORIZED, unknownCustomer.status());
  }

  @Test
  public void readOnlyUserIsAllowed() {
    when(mockHostCloudDetector.getHostCloud()).thenReturn(Optional.empty());
    Users readOnly =
        ModelFactory.testUser(
            Customer.getOrBadRequest(customerUuid), "ro@example.com", Users.Role.ReadOnly);

    Result result = get(customerUuid, readOnly.upsertApiToken());

    assertEquals(contentAsString(result), OK, result.status());
  }

  @Test
  public void connectOnlyUserIsUnauthorized() {
    Users connectOnly =
        ModelFactory.testUser(
            Customer.getOrBadRequest(customerUuid), "co@example.com", Users.Role.ConnectOnly);

    Result result = get(customerUuid, connectOnly.upsertApiToken());

    // The authorization layer answers a missing permission with 401.
    assertEquals(contentAsString(result), UNAUTHORIZED, result.status());
  }
}
