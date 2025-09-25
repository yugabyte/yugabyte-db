/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.ApiUtils.getDefaultUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.net.URLEncoder;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class UniverseControllerTest extends UniverseControllerTestBase {

  @Test
  public void testUniverseTrimFlags() {
    Map<String, String> data = new HashMap<>();
    data.put(" Test ", " One ");
    data.put(" Test 2 ", " Two ");

    Map<String, String> result = UniverseCRUDHandler.trimFlags(data);
    assertEquals(result.size(), 2);
    assertEquals(result.get("Test"), "One");
    assertEquals(result.get("Test 2"), "Two");
  }

  @Test
  @Parameters({
    "list universes, true, , GET",
    "get universe, false, , GET",
    "get universe leader, false, /leader, GET"
  })
  public void invalidCustomerUUID(
      String testDescription, boolean isList, String urlSuffix, String httpMethod) {
    UUID invalidCustomerUUID = UUID.randomUUID();
    String universesPath = isList ? "/universes" : "/universes/" + UUID.randomUUID();
    String url = "/api/customers/" + invalidCustomerUUID + universesPath + urlSuffix;
    Result result = doRequestWithAuthToken(httpMethod, url, authToken);
    assertEquals(url, UNAUTHORIZED, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.getUuid());
  }

  // TODO(vineeth) Decide: Should these result in FORBIDDEN after RBAC?
  @Test
  @Parameters({
    "get universe, , GET",
    "delete universe, , DELETE",
    "get universe status, /status, GET",
    "pause universe, /pause, POST",
    "resume universe, /resume, POST",
    "get universe leader, /leader, GET",
    "setup 2dc universe, /setup_universe_2dc, PUT"
  })
  public void invalidUniverseUUID(String testDescription, String urlSuffix, String httpMethod) {
    UUID randomUUID = UUID.randomUUID();
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + randomUUID + urlSuffix;
    Result result =
        assertPlatformException(() -> doRequestWithAuthToken(httpMethod, url, authToken));
    assertBadRequest(result, "Cannot find universe " + randomUUID);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testEmptyUniverseListWithValidUUID() {
    Result result = listUniverses();
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(json.size(), 0);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseListWithValidUUID() {
    Universe u = createUniverse(customer.getId());
    Result result = listUniverses();
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertNotNull(json);
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    assertValue(json.get(0), "universeUUID", u.getUniverseUUID().toString());
    assertAuditEntry(0, customer.getUuid());
  }

  private Result listUniverses() {
    return doRequestWithAuthToken(
        "GET", "/api/customers/" + customer.getUuid() + "/universes", authToken);
  }

  @Test
  @Parameters({
    "Fake Universe, 0",
    "Test Universe, 1",
  })
  public void testUniverseFindByName(String name, int expected) {
    UniverseDefinitionTaskParams.UserIntent ui = getDefaultUserIntent(customer);
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater(ui));
    String findUrl =
        "/api/customers/" + customer.getUuid() + "/universes?name=" + URLEncoder.encode(name);
    Result result = doRequestWithAuthToken("GET", findUrl, authToken);

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(expected, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseGetWithValidUniverseUUID() {
    UniverseDefinitionTaskParams.UserIntent ui = getDefaultUserIntent(customer);
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater(ui));

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + uUUID;
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    JsonNode clustersJson = universeDetails.get("clusters");
    assertNotNull(clustersJson);
    JsonNode primaryClusterJson = clustersJson.get(0);
    assertNotNull(primaryClusterJson);
    JsonNode userIntentJson = primaryClusterJson.get("userIntent");
    assertNotNull(userIntentJson);
    assertThat(userIntentJson.get("replicationFactor").asInt(), allOf(notNullValue(), equalTo(3)));

    JsonNode nodeDetailsMap = universeDetails.get("nodeDetailsSet");
    assertNotNull(nodeDetailsMap);
    assertNotNull(json.get("resources"));
    for (Iterator<JsonNode> it = nodeDetailsMap.elements(); it.hasNext(); ) {
      JsonNode node = it.next();
      int nodeIdx = node.get("nodeIdx").asInt();
      assertValue(node, "nodeName", "host-n" + nodeIdx);
    }
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseDestroyValidUUID() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result = doRequestWithAuthToken("DELETE", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Delete)));

    // TODO FIXME this assert is INVALID because it is on mockCommissioner
    // which never removes the universe. It was working before because
    // the customer in memory was never refreshed from the DB. Now that the
    // universe UUID is not stored in the customer and the getUniverseUUIDs()
    // makes a call to the DB, this starts failing.
    // assertTrue(customer.getUniverseUUIDs().isEmpty());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseDestroyValidUUIDIsForceDelete() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

    // Set http context
    TestUtils.setFakeHttpContext(user);
    UUID randUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    CustomerTask.create(
        customer,
        u.getUniverseUUID(),
        randUUID,
        CustomerTask.TargetType.Backup,
        CustomerTask.TaskType.Create,
        "test");

    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);

    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "?isForceDelete=true";
    Result result = doRequestWithAuthToken("DELETE", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Delete)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  // @formatter:off
  @Parameters({
    "true, true, false",
    "false, true, true",
    "true, false, false",
    "false, false, true",
    "null, true, false",
  })
  // @formatter:on
  public void testUniverseDestroyValidUUIDIsForceDeleteAndDeleteBackup(
      Boolean isDeleteBackups, Boolean isForceDelete, Boolean isDeleteAssociatedCerts) {
    String url;
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    String certificate = createTempFile("universe_controller_test_ca.crt", "test data");
    CertificateInfo certInfo = null;
    try {
      certInfo =
          ModelFactory.createCertificateInfo(
              customer.getUuid(), certificate, CertConfigType.SelfSigned);
    } catch (Exception e) {

    }

    Universe u = createUniverse(customer.getId(), certInfo.getUuid());

    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);

    Backup b =
        ModelFactory.createBackup(
            customer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    b.transitionState(Backup.BackupState.Completed);
    if (isDeleteBackups == null) {
      url =
          "/api/customers/"
              + customer.getUuid()
              + "/universes/"
              + u.getUniverseUUID()
              + "?isForceDelete="
              + isForceDelete
              + "&isDeleteAssociatedCerts="
              + isDeleteAssociatedCerts;
    } else {
      url =
          "/api/customers/"
              + customer.getUuid()
              + "/universes/"
              + u.getUniverseUUID()
              + "?isForceDelete="
              + isForceDelete
              + "&isDeleteBackups="
              + isDeleteBackups
              + "&isDeleteAssociatedCerts="
              + isDeleteAssociatedCerts;
    }
    Result result = doRequestWithAuthToken("DELETE", url, authToken);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertAuditEntry(1, customer.getUuid());
  }
}
