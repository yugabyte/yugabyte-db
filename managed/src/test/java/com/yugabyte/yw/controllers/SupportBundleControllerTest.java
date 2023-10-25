// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsBytes;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.EnumSet;
import java.util.List;
import java.util.UUID;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class SupportBundleControllerTest extends FakeDBApplication {

  private Customer customer;
  private Users user;
  private Universe universe;
  private SupportBundle mockSupportBundle = mock(SupportBundle.class);
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle/";

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("test-universe", customer.getId());
    user = ModelFactory.testUser(customer);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(fakeSupportBundleBasePath));
  }

  /* ==== Helper Request Functions ==== */

  private Result listSupportBundles(UUID customerUUID, UUID universeUUID) {
    String uri = "/api/customers/%s/universes/%s/support_bundle";
    return doRequestWithAuthToken(
        "GET",
        String.format(uri, customerUUID.toString(), universeUUID.toString()),
        user.createAuthToken());
  }

  private Result getSupportBundle(UUID customerUUID, UUID universeUUID, UUID supportBundleUUID) {
    String uri = "/api/customers/%s/universes/%s/support_bundle/%s";
    return doRequestWithAuthToken(
        "GET",
        String.format(
            uri, customerUUID.toString(), universeUUID.toString(), supportBundleUUID.toString()),
        user.createAuthToken());
  }

  private Result createSupportBundle(UUID customerUUID, UUID universeUUID, ObjectNode bodyJson) {
    String uri = "/api/customers/%s/universes/%s/support_bundle";
    return doRequestWithAuthTokenAndBody(
        "POST",
        String.format(uri, customerUUID.toString(), universeUUID.toString()),
        user.createAuthToken(),
        bodyJson);
  }

  private Result deleteSupportBundle(UUID customerUUID, UUID universeUUID, UUID supportBundleUUID) {
    String uri = "/api/customers/%s/universes/%s/support_bundle/%s";
    return doRequestWithAuthToken(
        "DELETE",
        String.format(
            uri, customerUUID.toString(), universeUUID.toString(), supportBundleUUID.toString()),
        user.createAuthToken());
  }

  private Result downloadSupportBundle(
      UUID customerUUID, UUID universeUUID, UUID supportBundleUUID) {
    String uri = "/api/customers/%s/universes/%s/support_bundle/%s/download";
    return doRequestWithAuthToken(
        "GET",
        String.format(
            uri, customerUUID.toString(), universeUUID.toString(), supportBundleUUID.toString()),
        user.createAuthToken());
  }

  private Result listSupportBundleComponents(UUID customerUUID) {
    String uri = "/api/customers/%s/support_bundle/components";
    return doRequestWithAuthToken(
        "GET", String.format(uri, customerUUID.toString()), user.createAuthToken());
  }

  /* ==== List Support Bundles API ==== */

  @Test
  public void testListSupportBundles() {
    // Create fake bundle entries in db table
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String bundlePath1 = "yb-support-bundle-" + "universe_name" + "-" + datePrefix + "-logs";
    SupportBundle sb1 =
        new SupportBundle(
            UUID.randomUUID(),
            universe.getUniverseUUID(),
            bundlePath1,
            new Date(),
            new Date(),
            new BundleDetails(EnumSet.allOf(BundleDetails.ComponentType.class)),
            SupportBundle.SupportBundleStatusType.Success);
    sb1.save();
    String bundlePath2 = "yb-support-bundle-" + "universe_name" + "-" + datePrefix + "-logs1";
    SupportBundle sb2 =
        new SupportBundle(
            UUID.randomUUID(),
            universe.getUniverseUUID(),
            bundlePath2,
            new Date(),
            new Date(),
            new BundleDetails(EnumSet.noneOf(BundleDetails.ComponentType.class)),
            SupportBundle.SupportBundleStatusType.Running);
    sb2.save();

    Result result = listSupportBundles(customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());

    List<SupportBundle> supportBundles = Json.fromJson(json, List.class);
    assertEquals(supportBundles.size(), 2);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListEmptySupportBundles() {
    Result result = listSupportBundles(customer.getUuid(), universe.getUniverseUUID());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());

    List<SupportBundle> supportBundles = Json.fromJson(json, List.class);
    assertEquals(supportBundles.size(), 0);
    assertAuditEntry(0, customer.getUuid());
  }

  /* ==== Get Single Support Bundle API ==== */

  @Test
  public void testGetValidSupportBundle() {
    // Create fake bundle entry in db table with status = success
    String datePrefix = new SimpleDateFormat("yyyyMMddHHmmss.SSS").format(new Date());
    String bundlePath1 = "yb-support-bundle-" + "universe_name" + "-" + datePrefix + "-logs";
    SupportBundle sb1 =
        new SupportBundle(
            UUID.randomUUID(),
            universe.getUniverseUUID(),
            bundlePath1,
            new Date(),
            new Date(),
            new BundleDetails(EnumSet.allOf(BundleDetails.ComponentType.class)),
            SupportBundle.SupportBundleStatusType.Success);
    sb1.save();

    Result result =
        getSupportBundle(customer.getUuid(), universe.getUniverseUUID(), sb1.getBundleUUID());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());

    SupportBundle supportBundle = Json.fromJson(json, SupportBundle.class);
    assertEquals(supportBundle, sb1);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGetInvalidSupportBundle() {
    // Trying to query for a bundle that doesn't exist
    Result result =
        assertPlatformException(
            () ->
                getSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), UUID.randomUUID()));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.getUuid());
  }

  /* ==== Create Support Bundle API ==== */

  @Test
  public void testCreateValidSupportBundle() {
    // Filling the JSON object to be passed in the request body
    ObjectNode bodyJson = Json.newObject();
    ObjectMapper mapper = new ObjectMapper();
    List<String> components =
        Arrays.asList(
            "UniverseLogs",
            "ApplicationLogs",
            "OutputFiles",
            "ErrorFiles",
            "CoreFiles",
            "GFlags",
            "Instance",
            "ConsensusMeta",
            "TabletMeta",
            "YbcLogs");
    ArrayNode componentsArray = mapper.valueToTree(components);

    bodyJson.put("startDate", "2022-02-01T12:21:45Z");
    bodyJson.put("endDate", "2022-03-03T12:21:45Z");
    bodyJson.putArray("components").addAll(componentsArray);

    // Mocking commissioner submit functionality to create a support bundle
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.CreateSupportBundle);
    when(mockCommissioner.submit(any(TaskType.class), any(SupportBundleTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Result result = createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertEquals(json.get("taskUUID").asText(), fakeTaskUUID.toString());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateSupportBundleWithUniverseUpdateInProgress() {
    // Filling the JSON object to be passed in the request body
    ObjectNode bodyJson = Json.newObject();
    ObjectMapper mapper = new ObjectMapper();
    List<String> components =
        Arrays.asList(
            "UniverseLogs",
            "ApplicationLogs",
            "OutputFiles",
            "ErrorFiles",
            "CoreFiles",
            "GFlags",
            "Instance",
            "ConsensusMeta",
            "TabletMeta",
            "YbcLogs");
    ArrayNode componentsArray = mapper.valueToTree(components);

    bodyJson.put("startDate", "2022-02-01T00:00:00Z");
    bodyJson.put("endDate", "2022-03-03T00:00:00Z");
    bodyJson.putArray("components").addAll(componentsArray);

    // Mocking commissioner submit functionality to create a support bundle
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.CreateSupportBundle);
    lenient()
        .when(mockCommissioner.submit(any(TaskType.class), any(SupportBundleTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    // Changing universe state updateInProgress = true
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.updateInProgress = true;
              universe.setUniverseDetails(universeDetails);
            });

    Result result = createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateSupportBundleWithUniversePaused() {
    // Filling the JSON object to be passed in the request body
    ObjectNode bodyJson = Json.newObject();
    ObjectMapper mapper = new ObjectMapper();
    List<String> components =
        Arrays.asList(
            "UniverseLogs",
            "ApplicationLogs",
            "OutputFiles",
            "ErrorFiles",
            "CoreFiles",
            "GFlags",
            "Instance",
            "ConsensusMeta",
            "TabletMeta",
            "YbcLogs");
    ArrayNode componentsArray = mapper.valueToTree(components);

    bodyJson.put("startDate", "2022-02-01T00:00:00Z");
    bodyJson.put("endDate", "2022-03-03T00:00:00Z");
    bodyJson.putArray("components").addAll(componentsArray);

    // Mocking commissioner submit functionality to create a support bundle
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.CreateSupportBundle);
    lenient()
        .when(mockCommissioner.submit(any(TaskType.class), any(SupportBundleTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    // Changing universe state universePaused = true
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.universePaused = true;
              universe.setUniverseDetails(universeDetails);
            });

    Result result = createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateSupportBundleWithOnpremUniverse() {
    // Filling the JSON object to be passed in the request body
    ObjectNode bodyJson = Json.newObject();
    ObjectMapper mapper = new ObjectMapper();
    List<String> components =
        Arrays.asList(
            "UniverseLogs",
            "ApplicationLogs",
            "OutputFiles",
            "ErrorFiles",
            "CoreFiles",
            "GFlags",
            "Instance",
            "ConsensusMeta",
            "TabletMeta",
            "YbcLogs");
    ArrayNode componentsArray = mapper.valueToTree(components);

    bodyJson.put("startDate", "2022-02-01T22:45:21Z");
    bodyJson.put("endDate", "2022-03-03T22:45:21Z");
    bodyJson.putArray("components").addAll(componentsArray);

    // Mocking commissioner submit functionality to create a support bundle
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.CreateSupportBundle);
    lenient()
        .when(mockCommissioner.submit(any(TaskType.class), any(SupportBundleTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    // Changing provider type -> onprem
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.getPrimaryCluster().userIntent.providerType = CloudType.onprem;
              universe.setUniverseDetails(universeDetails);
            });

    Result result = createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateSupportBundleWithKubernetesUniverse() {
    // Filling the JSON object to be passed in the request body
    ObjectNode bodyJson = Json.newObject();
    ObjectMapper mapper = new ObjectMapper();
    List<String> components =
        Arrays.asList(
            "UniverseLogs",
            "ApplicationLogs",
            "OutputFiles",
            "ErrorFiles",
            "CoreFiles",
            "GFlags",
            "Instance",
            "ConsensusMeta",
            "TabletMeta",
            "YbcLogs");
    ArrayNode componentsArray = mapper.valueToTree(components);

    bodyJson.put("startDate", "2022-02-01T12:34:45Z");
    bodyJson.put("endDate", "2022-03-03T12:34:45Z");
    bodyJson.putArray("components").addAll(componentsArray);

    // Mocking commissioner submit functionality to create a support bundle
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.CreateSupportBundle);
    lenient()
        .when(mockCommissioner.submit(any(TaskType.class), any(SupportBundleTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    // Changing provider type -> kubernetes
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.getPrimaryCluster().userIntent.providerType = CloudType.kubernetes;
              universe.setUniverseDetails(universeDetails);
            });

    Result result = createSupportBundle(customer.getUuid(), universe.getUniverseUUID(), bodyJson);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.getUuid());
  }

  /* ==== Delete Support Bundle API ==== */

  @Test
  public void testDeleteValidSupportBundle() {
    UUID bundleUUID = UUID.randomUUID();
    String fakeSupportBundleFileName = bundleUUID.toString() + ".tar.gz";
    createTempFile(fakeSupportBundleBasePath, fakeSupportBundleFileName, "test-bundle-content");

    // Create fake bundle entry in db table
    SupportBundle sb1 =
        new SupportBundle(
            bundleUUID,
            universe.getUniverseUUID(),
            fakeSupportBundleBasePath + fakeSupportBundleFileName,
            new Date(),
            new Date(),
            new BundleDetails(EnumSet.allOf(BundleDetails.ComponentType.class)),
            SupportBundle.SupportBundleStatusType.Success);
    sb1.save();

    Result result =
        deleteSupportBundle(customer.getUuid(), universe.getUniverseUUID(), sb1.getBundleUUID());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testDeleteInvalidSupportBundle() {
    UUID bundleUUID = UUID.randomUUID();
    String fakeSupportBundleFileName = bundleUUID.toString() + ".tar.gz";
    createTempFile(fakeSupportBundleBasePath, fakeSupportBundleFileName, "test-bundle-content");

    // Create fake bundle entry in db table
    SupportBundle sb1 =
        new SupportBundle(
            bundleUUID,
            universe.getUniverseUUID(),
            fakeSupportBundleBasePath + fakeSupportBundleFileName,
            new Date(),
            new Date(),
            new BundleDetails(EnumSet.allOf(BundleDetails.ComponentType.class)),
            SupportBundle.SupportBundleStatusType.Running);
    sb1.save();

    Result result =
        assertPlatformException(
            () ->
                deleteSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), sb1.getBundleUUID()));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.getUuid());
  }

  /* ==== Download Support Bundle API ==== */

  @Test
  public void testDownloadValidSupportBundle() {
    UUID bundleUUID = UUID.randomUUID();
    String fakeSupportBundleFileName = bundleUUID.toString() + ".tar.gz";
    String fakeSupportBundlePath =
        createTempFile(fakeSupportBundleBasePath, fakeSupportBundleFileName, "test-bundle-content");

    // Create fake bundle entry in db table
    SupportBundle sb1 =
        new SupportBundle(
            bundleUUID,
            universe.getUniverseUUID(),
            fakeSupportBundleBasePath + fakeSupportBundleFileName,
            new Date(),
            new Date(),
            new BundleDetails(EnumSet.allOf(BundleDetails.ComponentType.class)),
            SupportBundle.SupportBundleStatusType.Success);
    sb1.save();

    Result result =
        downloadSupportBundle(customer.getUuid(), universe.getUniverseUUID(), sb1.getBundleUUID());
    assertEquals(OK, result.status());
    try {
      // Read the byte array received from the server and compare with original
      byte[] fakeBundleContent = Files.readAllBytes(new File(fakeSupportBundlePath).toPath());
      byte[] actualBundleContent = contentAsBytes(result, mat).toArray();
      assertArrayEquals(fakeBundleContent, actualBundleContent);
    } catch (Exception e) {
    }
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDownloadInvalidSupportBundle() {
    UUID bundleUUID = UUID.randomUUID();
    String fakeSupportBundleFileName = bundleUUID.toString() + ".tar.gz";
    String fakeSupportBundlePath =
        createTempFile(fakeSupportBundleBasePath, fakeSupportBundleFileName, "test-bundle-content");

    // Create fake bundle entry in db table
    SupportBundle sb1 =
        new SupportBundle(
            bundleUUID,
            universe.getUniverseUUID(),
            fakeSupportBundleBasePath + fakeSupportBundleFileName,
            new Date(),
            new Date(),
            new BundleDetails(EnumSet.allOf(BundleDetails.ComponentType.class)),
            SupportBundle.SupportBundleStatusType.Running);
    sb1.save();

    Result result =
        assertPlatformException(
            () ->
                downloadSupportBundle(
                    customer.getUuid(), universe.getUniverseUUID(), sb1.getBundleUUID()));
    assertEquals(NOT_FOUND, result.status());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListSupportBundleComponents() {
    Result result = listSupportBundleComponents(customer.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());

    List<ComponentType> copmponents = Json.fromJson(json, List.class);
    assertEquals(copmponents.size(), ComponentType.values().length);
    assertAuditEntry(0, customer.getUuid());
  }
}
