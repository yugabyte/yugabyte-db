package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertErrorResponse;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ModelFactory.awsProvider;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.PlacementInfoUtil.getAzUuidToNumNodes;
import static com.yugabyte.yw.common.PlacementInfoUtil.updateUniverseDefinition;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.EDIT;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.databind.type.TypeFactory;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class UniverseUiOnlyControllerTest extends UniverseCreateControllerTestBase {

  @Override
  public Result sendCreateRequest(ObjectNode bodyJson) {
    return doRequestWithAuthTokenAndBody(
        "POST", "/api/customers/" + customer.getUuid() + "/universes", authToken, bodyJson);
  }

  @Override
  public Result sendPrimaryCreateConfigureRequest(ObjectNode bodyJson) {
    bodyJson.put("currentClusterType", "PRIMARY");
    bodyJson.put("clusterOperation", "CREATE");
    return sendOldApiConfigureRequest(bodyJson);
  }

  @Override
  public Result sendPrimaryEditConfigureRequest(ObjectNode bodyJson) {
    bodyJson.put("currentClusterType", "PRIMARY");
    bodyJson.put("clusterOperation", "EDIT");
    return sendOldApiConfigureRequest(bodyJson);
  }

  public Result sendAsyncCreateConfigureRequest(ObjectNode topJson) {
    topJson.put("currentClusterType", "ASYNC");
    topJson.put("clusterOperation", "CREATE");
    return sendOldApiConfigureRequest(topJson);
  }

  public Result sendOldApiConfigureRequest(ObjectNode topJson) {
    return doRequestWithAuthTokenAndBody(
        "POST", "/api/customers/" + customer.getUuid() + "/universe_configure", authToken, topJson);
  }

  @Override
  protected JsonNode getUniverseJson(Result universeCreateResponse) {
    return Json.parse(contentAsString(universeCreateResponse));
  }

  @Override
  protected JsonNode getUniverseDetailsJson(Result universeConfigureResponse) {
    return Json.parse(contentAsString(universeConfigureResponse));
  }

  /** Migrated to {@link UniverseControllerTest#testUniverseFindByName(String, int)} */
  @Test
  @Parameters({
    "FakeUniverse, 0",
    "TestUniverse, 1",
  })
  public void testFind(String name, int expected) {
    createUniverse("TestUniverse", customer.getId());
    String url = "/api/customers/" + customer.getUuid() + "/universes/find?name=" + name;
    Result result = doRequestWithAuthToken("GET", url, authToken);

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isArray());
    assertEquals(expected, json.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testConfigureInvalidParams() {
    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "Foo_Bar")
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", UUID.randomUUID().toString());
    ArrayNode regionList = Json.newArray().add(UUID.randomUUID().toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.put("clusterOperation", "CREATE");

    Result result = assertPlatformException(() -> sendOldApiConfigureRequest(bodyJson));
    assertBadRequest(result, "currentClusterType");

    bodyJson.remove("clusterOperation");
    bodyJson.put("currentClusterType", "PRIMARY");
    result = assertPlatformException(() -> sendOldApiConfigureRequest(bodyJson));
    assertBadRequest(result, "clusterOperation");
  }

  @Test
  public void testConfigureCreateWithReadOnlyClusters() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");

    Region rReadOnly = Region.create(p, "region-readOnly-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(rReadOnly, "az-readOnly-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(rReadOnly, "az-readOnly-2", "PlacementAZ 2", "subnet-2");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univWithReadOnlyCreate";
    UUID readOnlyUuid0 = UUID.randomUUID();
    UUID readOnlyUuid1 = UUID.randomUUID();
    taskParams.upsertPrimaryCluster(getTestUserIntent(r, p, i, 5), null);
    taskParams.upsertCluster(getTestUserIntent(rReadOnly, p, i, 5), null, readOnlyUuid0);
    taskParams.upsertCluster(getTestUserIntent(rReadOnly, p, i, 5), null, readOnlyUuid1);

    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    PlacementInfoUtil.updateUniverseDefinition(taskParams, customer.getId(), readOnlyUuid0, CREATE);
    PlacementInfoUtil.updateUniverseDefinition(taskParams, customer.getId(), readOnlyUuid1, CREATE);

    UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams.getPrimaryCluster();
    List<PlacementInfo.PlacementAZ> azList =
        primaryCluster.placementInfo.cloudList.get(0).regionList.get(0).azList;
    assertEquals(azList.size(), 2);

    UniverseDefinitionTaskParams.Cluster readOnlyCluster0 =
        taskParams.getClusterByUuid(readOnlyUuid0);
    azList = readOnlyCluster0.placementInfo.cloudList.get(0).regionList.get(0).azList;
    assertEquals(azList.size(), 2);

    UniverseDefinitionTaskParams.Cluster readOnlyCluster1 =
        taskParams.getClusterByUuid(readOnlyUuid1);
    azList = readOnlyCluster1.placementInfo.cloudList.get(0).regionList.get(0).azList;
    assertEquals(azList.size(), 2);

    Map<UUID, Integer> azUUIDToNumNodeMap = getAzUuidToNumNodes(primaryCluster.placementInfo);
    Map<UUID, Integer> azUUIDToNumNodeMapReadOnly0 =
        getAzUuidToNumNodes(readOnlyCluster0.placementInfo);
    Map<UUID, Integer> azUUIDToNumNodeMapReadOnly1 =
        getAzUuidToNumNodes(readOnlyCluster1.placementInfo);
    for (Map.Entry<UUID, Integer> entry : azUUIDToNumNodeMapReadOnly0.entrySet()) {
      UUID uuid = entry.getKey();
      int numNodes = entry.getValue();
      if (azUUIDToNumNodeMap.containsKey(uuid)) {
        int prevNumNodes = azUUIDToNumNodeMap.get(uuid);
        azUUIDToNumNodeMap.put(uuid, prevNumNodes + numNodes);
      } else {
        azUUIDToNumNodeMap.put(uuid, numNodes);
      }
    }
    for (Map.Entry<UUID, Integer> entry : azUUIDToNumNodeMapReadOnly1.entrySet()) {
      UUID uuid = entry.getKey();
      int numNodes = entry.getValue();
      if (azUUIDToNumNodeMap.containsKey(uuid)) {
        int prevNumNodes = azUUIDToNumNodeMap.get(uuid);
        azUUIDToNumNodeMap.put(uuid, prevNumNodes + numNodes);
      } else {
        azUUIDToNumNodeMap.put(uuid, numNodes);
      }
    }
    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    Result result = sendAsyncCreateConfigureRequest(topJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("nodeDetailsSet").isArray());
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");
    assertEquals(15, nodeDetailJson.size());
    assertTrue(areConfigObjectsEqual(nodeDetailJson, azUUIDToNumNodeMap));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testConfigureEditOnPremInvalidNodeAZCombo_fail() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone az2 = AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");

    List<AvailabilityZone> azList = new ArrayList<>();
    azList.add(az1);
    azList.add(az2);

    UniverseDefinitionTaskParams taskParams = setupOnPremTestData(6, p, r, azList);

    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "type.small", 10, 5.5, new InstanceType.InstanceTypeDetails());

    taskParams.nodePrefix = "test_uni";
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, p, i, 5);
    userIntent.providerType = Common.CloudType.onprem;
    userIntent.instanceType = "type.small";
    userIntent.universeName = "megauniverse";
    taskParams.upsertPrimaryCluster(userIntent, null);
    UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams.getPrimaryCluster();
    updateUniverseDefinition(taskParams, customer.getId(), primaryCluster.uuid, CREATE);

    Universe.create(taskParams, customer.getId());

    // Set the nodes state to inUse
    int k = 0;
    for (NodeInstance ni : NodeInstance.listByProvider(p.getUuid())) {
      if (k < 5) {
        k++;
        ni.setInUse(true);
        ni.save();
      } else {
        break;
      }
    }

    // Simulate a running universe by setting existing nodes to Live state.
    for (NodeDetails nd : taskParams.nodeDetailsSet) {
      nd.state = NodeState.Live;
    }

    PlacementInfo placementInfo = taskParams.getPrimaryCluster().placementInfo;
    placementInfo.azStream().findFirst().get().numNodesInAZ += 7;
    taskParams.userAZSelected = true;
    // HERE
    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    Result result = assertPlatformException(() -> sendPrimaryEditConfigureRequest(topJson));
    assertBadRequest(result, "Couldn't find 12 nodes of type type.small in PlacementAZ 1");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomConfigureEditWithPureExpand() {
    Provider p = ModelFactory.awsProvider(customer);
    Universe u = createUniverse(customer.getId());

    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    UniverseDefinitionTaskParams utd = new UniverseDefinitionTaskParams();
    utd.setUniverseUUID(u.getUniverseUUID());
    UniverseDefinitionTaskParams.UserIntent ui = getTestUserIntent(r, p, i, 5);
    ui.universeName = u.getName();
    ui.ybSoftwareVersion = "1.0";
    ui.preferredRegion = ui.regionList.get(0);
    utd.upsertPrimaryCluster(ui, null);
    PlacementInfoUtil.updateUniverseDefinition(
        utd,
        customer.getId(),
        utd.getPrimaryCluster().uuid,
        UniverseConfigureTaskParams.ClusterOperationType.CREATE);
    Universe.UniverseUpdater updater = universe -> universe.setUniverseDetails(utd);
    Universe.saveDetails(u.getUniverseUUID(), updater);
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    int totalNumNodesAfterExpand = 0;
    Map<UUID, Integer> azUuidToNumNodes =
        getAzUuidToNumNodes(u.getUniverseDetails().nodeDetailsSet);
    for (Map.Entry<UUID, Integer> entry : azUuidToNumNodes.entrySet()) {
      totalNumNodesAfterExpand += entry.getValue() + 1;
      azUuidToNumNodes.put(entry.getKey(), entry.getValue() + 1);
    }
    UniverseDefinitionTaskParams editTestUTD = u.getUniverseDetails();
    UniverseDefinitionTaskParams.Cluster primaryCluster = editTestUTD.getPrimaryCluster();
    primaryCluster.userIntent.numNodes = totalNumNodesAfterExpand;
    primaryCluster.placementInfo = constructPlacementInfoObject(azUuidToNumNodes);

    ObjectNode editJson = (ObjectNode) Json.toJson(editTestUTD);
    Result result = sendPrimaryEditConfigureRequest(editJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("nodeDetailsSet").isArray());
    ArrayNode nodeDetailJson = (ArrayNode) json.get("nodeDetailsSet");
    assertEquals(nodeDetailJson.size(), totalNumNodesAfterExpand);
    assertTrue(areConfigObjectsEqual(nodeDetailJson, azUuidToNumNodes));
    assertAuditEntry(1, customer.getUuid());
    assertTrue(json.get("updateOptions").isArray());
    ArrayNode updateOptionsJson = (ArrayNode) json.get("updateOptions");
    assertEquals(1, updateOptionsJson.size());
    assertEquals("FULL_MOVE", updateOptionsJson.get(0).asText());
  }

  @Test
  public void testOnPremConfigureValidAZNodeComboNonEmptyNodeDetailsSet() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");

    List<AvailabilityZone> azList = new ArrayList<>();
    azList.add(az1);

    UniverseDefinitionTaskParams taskParams = setupOnPremTestData(6, p, r, azList);

    UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams.getPrimaryCluster();

    updateUniverseDefinition(taskParams, customer.getId(), primaryCluster.uuid, CREATE);

    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    Result result = sendPrimaryCreateConfigureRequest(topJson);
    assertOk(result);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateRegionsFilled() {
    Provider p = ModelFactory.newProvider(customer, Common.CloudType.onprem);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone az1 = AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");

    List<AvailabilityZone> azList = new ArrayList<>();
    azList.add(az1);

    UniverseDefinitionTaskParams taskParams = setupOnPremTestData(6, p, r, azList);

    UniverseDefinitionTaskParams.Cluster primaryCluster = taskParams.getPrimaryCluster();

    updateUniverseDefinition(taskParams, customer.getId(), primaryCluster.uuid, CREATE);

    ObjectNode topJson = (ObjectNode) Json.toJson(taskParams);
    Result result = sendPrimaryCreateConfigureRequest(topJson);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    ArrayNode regionsArray = (ArrayNode) resultJson.get("clusters").get(0).get("regions");
    assertFalse(regionsArray.isEmpty());
    Region resultReginon = Json.fromJson(regionsArray.get(0), Region.class);
    assertThat(resultReginon, equalTo(r));
  }

  @Test
  public void testUniverseCreateWithInvalidTServerJson() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CheckTServers);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u = createUniverse(customer.getId());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("masterGFlags", "abcd")
            .put("universeName", u.getName())
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    Cluster cluster = u.getUniverseDetails().clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.getUniverseUUID().toString());
    assertNotNull(json.get("universeDetails"));
    assertTrue(json.get("universeConfig").asText().isEmpty());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Update)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseExpand() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u = createUniverse(customer.getId());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("numNodes", 5)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    Cluster cluster = u.getUniverseDetails().clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.getUniverseUUID().toString());
    JsonNode universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    JsonNode clustersJson = universeDetails.get("clusters");
    assertNotNull(clustersJson);
    JsonNode primaryClusterJson = clustersJson.get(0);
    assertNotNull(primaryClusterJson);
    assertNotNull(primaryClusterJson.get("userIntent"));
    assertAuditEntry(1, customer.getUuid());

    fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    // Try universe expand only, and re-check.
    userIntentJson.put("numNodes", 9);
    result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.getUniverseUUID().toString());
    universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    clustersJson = universeDetails.get("clusters");
    assertNotNull(clustersJson);
    primaryClusterJson = clustersJson.get(0);
    assertNotNull(primaryClusterJson);
    assertNotNull(primaryClusterJson.get("userIntent"));
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testUniverseExpandWithYbc() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u =
        createUniverse(
            "Test Universe", UUID.randomUUID(), customer.getId(), CloudType.aws, null, null, true);
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("numNodes", 5)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    Cluster cluster = u.getUniverseDetails().clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());
    bodyJson.put("enableYbc", true);
    bodyJson.put("ybcSoftwareVersion", "");

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.getUniverseUUID().toString());
    JsonNode universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    JsonNode clustersJson = universeDetails.get("clusters");
    assertNotNull(clustersJson);
    JsonNode primaryClusterJson = clustersJson.get(0);
    assertNotNull(primaryClusterJson);
    assertNotNull(primaryClusterJson.get("userIntent"));
    assertAuditEntry(1, customer.getUuid());

    fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    // Try universe expand only, and re-check.
    userIntentJson.put("numNodes", 9);
    result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.getUniverseUUID().toString());
    universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    clustersJson = universeDetails.get("clusters");
    assertNotNull(clustersJson);
    primaryClusterJson = clustersJson.get(0);
    assertNotNull(primaryClusterJson);
    assertNotNull(primaryClusterJson.get("userIntent"));
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testUniverseExpandWithTransitNodes() {
    UUID fakeTaskUUID = UUID.randomUUID();

    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u = createUniverse(customer.getId());
    Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    setInTransitNode(u.getUniverseUUID());
    ObjectNode bodyJson = Json.newObject();
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("numNodes", 5)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.getUuid().toString());
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertBadRequest(result, "as it has nodes in one of");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseUpdateWithInvalidParams() {
    Universe u = createUniverse(customer.getId());
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, Json.newObject()));
    assertBadRequest(result, "clusters: This field is required");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseUpdateWithChangingIP() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    Universe u = createUniverse(customer.getId());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("numNodes", 3)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode nodeDetailsJsonArray = Json.newArray();
    Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster cluster = universeDetails.clusters.get(0);
          cluster.userIntent = Json.fromJson(userIntentJson, UserIntent.class);
          for (int idx = 0; idx < 3; idx++) {
            NodeDetails node = ApiUtils.getDummyNodeDetails(idx, NodeState.Live);
            node.placementUuid = cluster.uuid;
            nodeDetailsJsonArray.add(Json.toJson(node));
            if (idx == 1) {
              node.cloudInfo.private_ip += "1"; // changing ip from 10.0.0.1 to 10.0.0.11
            }
            universeDetails.nodeDetailsSet.add(node);
          }
        });

    Cluster cluster = u.getUniverseDetails().clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", nodeDetailsJsonArray);

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertBadRequest(result, "Illegal attempt to change private ip to 10.0.0.1 for node host-n1");
  }

  @Test
  public void testUniverseUpdateIllegalIP() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    Universe u = createUniverse(customer.getId());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("numNodes", 3)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.getUuid().toString())
            .put("providerType", p.getCode())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode nodeDetailsJsonArray = Json.newArray();
    Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster cluster = universeDetails.clusters.get(0);
          cluster.userIntent = Json.fromJson(userIntentJson, UserIntent.class);
          for (int idx = 0; idx < 3; idx++) {
            NodeDetails node = ApiUtils.getDummyNodeDetails(idx, NodeState.ToBeAdded);
            node.nodeName = "";
            node.placementUuid = cluster.uuid;
            if (idx == 0) {
              node.cloudInfo.private_ip = FORBIDDEN_IP_2;
            }
            nodeDetailsJsonArray.add(Json.toJson(node));
            universeDetails.nodeDetailsSet.add(node);
          }
        });

    Cluster cluster = u.getUniverseDetails().clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", nodeDetailsJsonArray);

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertBadRequest(result, String.format("Forbidden ip %s for node", FORBIDDEN_IP_2));
  }

  @Test
  public void testUniverseUpdateWithAllNodesRemoved() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    Universe u = createUniverse(customer.getId());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("numNodes", 3)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);
    ArrayNode nodeDetailsJsonArray = Json.newArray();
    Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster cluster = universeDetails.clusters.get(0);
          cluster.userIntent = Json.fromJson(userIntentJson, UserIntent.class);
          for (int idx = 0; idx < 3; idx++) {
            NodeDetails node = ApiUtils.getDummyNodeDetails(idx, NodeState.ToBeRemoved);
            node.placementUuid = cluster.uuid;
            nodeDetailsJsonArray.add(Json.toJson(node));
            universeDetails.nodeDetailsSet.add(node);
          }
        });

    Cluster cluster = u.getUniverseDetails().clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", nodeDetailsJsonArray);

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertBadRequest(result, "All nodes cannot be removed for cluster " + cluster.uuid);
  }

  @Test
  public void testUniverseUpdateWithMissingNodes() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    Universe u = createUniverse(customer.getId());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("numNodes", 3)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode);

    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);

    ArrayNode nodeDetailsJsonArray = Json.newArray();
    Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster cluster = universeDetails.clusters.get(0);
          cluster.userIntent = Json.fromJson(userIntentJson, UserIntent.class);
          for (int idx = 0; idx < 3; idx++) {
            NodeDetails node = ApiUtils.getDummyNodeDetails(idx, NodeState.Live);
            node.placementUuid = cluster.uuid;
            if (idx > 0) {
              // Exclude the first node in the payload.
              nodeDetailsJsonArray.add(Json.toJson(node));
            }
            universeDetails.nodeDetailsSet.add(node);
          }
        });
    UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
    Cluster cluster = universeDetails.clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", nodeDetailsJsonArray);
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertBadRequest(result, "Node host-n0 is missing");
  }

  @Test
  public void testUniverseUpdateWithUnknownNodes() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    Universe u = createUniverse(customer.getId());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("numNodes", 3)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode);

    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);

    ArrayNode nodeDetailsJsonArray = Json.newArray();
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              Cluster cluster = universeDetails.clusters.get(0);
              cluster.userIntent = Json.fromJson(userIntentJson, UserIntent.class);
              for (int idx = 0; idx < 3; idx++) {
                NodeDetails node = ApiUtils.getDummyNodeDetails(idx, NodeState.Live);
                node.placementUuid = cluster.uuid;
                nodeDetailsJsonArray.add(Json.toJson(node));
                if (idx > 0) {
                  // Exclude the first node in the universe.
                  universeDetails.nodeDetailsSet.add(node);
                }
              }
            });
    UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
    Cluster cluster = universeDetails.clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", nodeDetailsJsonArray);
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertBadRequest(result, "Unknown nodes host-n0");
  }

  @Test
  public void testUniverseUpdateWithNodeNameInToBeAdded() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    Universe u = createUniverse(customer.getId());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("numNodes", 3)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode);
    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);

    Cluster cluster = u.getUniverseDetails().clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);

    ArrayNode nodeDetailsJsonArray = Json.newArray();
    for (int idx = 0; idx < 3; idx++) {
      NodeDetails node = ApiUtils.getDummyNodeDetails(idx, NodeState.Live);
      node.placementUuid = cluster.uuid;
      if (idx == 0) {
        node.state = NodeState.ToBeAdded;
      }
      nodeDetailsJsonArray.add(Json.toJson(node));
    }

    ObjectNode bodyJson = Json.newObject();
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", nodeDetailsJsonArray);

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertBadRequest(result, "Node name host-n0 cannot be present");
  }

  @Test
  public void testUniverseUpdateWithValidParams() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Provider p = ModelFactory.awsProvider(customer);
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u = createUniverse(customer.getId());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", u.getName())
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("numNodes", 3)
            .put("provider", p.getUuid().toString())
            .put("accessKeyCode", accessKeyCode)
            .set("deviceInfo", Json.toJson(ApiUtils.getDummyDeviceInfo(1, 1)));

    ArrayNode regionList = Json.newArray().add(r.getUuid().toString());
    userIntentJson.set("regionList", regionList);

    ArrayNode nodeDetailsJsonArray = Json.newArray();
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              Cluster cluster = universeDetails.clusters.get(0);
              cluster.userIntent = Json.fromJson(userIntentJson, UserIntent.class);
              for (int idx = 0; idx < 3; idx++) {
                NodeDetails node = ApiUtils.getDummyNodeDetails(idx, NodeState.Live);
                node.placementUuid = cluster.uuid;
                nodeDetailsJsonArray.add(Json.toJson(node));
                universeDetails.nodeDetailsSet.add(node);
              }
            });
    UniverseDefinitionTaskParams universeDetails = u.getUniverseDetails();
    Cluster cluster = universeDetails.clusters.get(0);
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("uuid", Json.toJson(cluster.uuid));
    ArrayNode clustersJsonArray = Json.newArray().add(clusterJson);

    ObjectNode bodyJson = Json.newObject();
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", nodeDetailsJsonArray);

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "universeUUID", u.getUniverseUUID().toString());
    assertNotNull(json.get("universeDetails"));

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Update)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testVMImageUpgradeWithUnsupportedProvider() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);

    UUID fakeTaskUUID = UUID.randomUUID();
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    Provider p = ModelFactory.onpremProvider(customer);
    ObjectNode bodyJson = setupVMImageUpgradeParams(uUUID, p, "type.small");

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + uUUID + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "VM image upgrade is only supported for AWS / GCP");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testVMImageUpgradeWithEphemerals() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);

    UUID fakeTaskUUID = UUID.randomUUID();
    Universe universe = createUniverse(customer.getId());
    UUID uUUID = universe.getUniverseUUID();
    CloudType providerType =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    Universe.saveDetails(
        uUUID,
        univ -> {
          ApiUtils.mockUniverseUpdater(providerType).run(univ);
          univ.getUniverseDetails()
              .nodeDetailsSet
              .forEach(node -> node.cloudInfo.instance_type = "i3.xlarge");
        });

    Provider p = ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = setupVMImageUpgradeParams(uUUID, p, "i3.xlarge");

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + uUUID + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Cannot upgrade a universe with ephemeral storage");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testVMImageUpgradeWithNoImage() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);

    UUID fakeTaskUUID = UUID.randomUUID();
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    Provider p = ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = setupVMImageUpgradeParams(uUUID, p, "c5.xlarge");

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + uUUID + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "machineImages param is required for taskType: VMImage");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testVMImageUpgradeValidParams() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.VMImageUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Universe u = createUniverse(customer.getId());
    Provider p = ModelFactory.awsProvider(customer);
    ObjectNode bodyJson = setupVMImageUpgradeParams(u.getUniverseUUID(), p, "c5.xlarge");
    ObjectNode images = Json.newObject();
    UUID r = UUID.randomUUID();
    images.put(r.toString(), "image-" + r.toString());
    bodyJson.set("machineImages", images);
    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeVMImage)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseUpgradeWithEmptyParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    String url = "/api/customers/" + customer.getUuid() + "/universes/" + uUUID + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, Json.newObject()));
    assertBadRequest(result, "clusters: This field is required");
    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  @Parameters({
    "Rolling",
    "Non-Rolling",
    "Non-Restart",
  })
  public void testUniverseUpgradeWithNodesInTransit(String upgradeOption) {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.UpgradeUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe.saveDetails(uUUID, ApiUtils.mockUniverseUpdater());

    setInTransitNode(uUUID);

    ObjectNode bodyJson = getValidPayload(uUUID, upgradeOption);
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + uUUID + "/upgrade";
    if (upgradeOption.equals("Rolling")) {
      Result result =
          assertPlatformException(
              () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
      assertBadRequest(result, "as it has nodes in one of");
      assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
      assertAuditEntry(0, customer.getUuid());
    } else {
      Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
      assertOk(result);
      JsonNode json = Json.parse(contentAsString(result));
      assertValue(json, "taskUUID", fakeTaskUUID.toString());
      assertAuditEntry(1, customer.getUuid());
    }
  }

  ObjectNode getValidPayload(UUID univUUID, String upgradeOption) {
    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", univUUID.toString())
            .put("taskType", "Software")
            .put("upgradeOption", upgradeOption)
            .put("ybSoftwareVersion", "0.0.1");
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "Single UserUniverse")
            .put("ybSoftwareVersion", "0.0.1");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    return bodyJson;
  }

  protected ObjectNode setupVMImageUpgradeParams(UUID uUUID, Provider p, String instanceType) {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);

    ObjectNode bodyJson = getValidPayload(uUUID, "Rolling");
    bodyJson.put("taskType", UpgradeTaskType.VMImage.toString());
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), instanceType, 10, 5.5, new InstanceType.InstanceTypeDetails());
    ObjectNode userIntentJson =
        Json.newObject()
            .put("numNodes", 3)
            .put("instanceType", i.getInstanceTypeCode())
            .put("replicationFactor", 3)
            .put("providerType", p.getCode())
            .put("provider", p.getUuid().toString());
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    return bodyJson;
  }

  @Test
  public void testUniverseSoftwareUpgradeValidParams() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.SoftwareUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson = getValidPayload(u.getUniverseUUID(), "Rolling");
    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeSoftware)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseRollingRestartValidParams() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.RestartUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "Restart")
            .put("upgradeOption", "Rolling");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Restart)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseRollingRestartNonRolling() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.RestartUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "Restart")
            .put("upgradeOption", "Non-Rolling");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);
    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Rolling restart has to be a ROLLING UPGRADE.");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseSoftwareUpgradeWithInvalidParams() {
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "Software");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "ybSoftwareVersion param is required for taskType: Software");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseGFlagsUpgradeValidParams() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "GFlags");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    JsonNode masterGFlags = Json.parse("[{ \"name\": \"master-flag\", \"value\": \"123\"}]");
    JsonNode tserverGFlags = Json.parse("[{ \"name\": \"tserver-flag\", \"value\": \"456\"}]");
    userIntentJson.set("masterGFlags", masterGFlags);
    userIntentJson.set("tserverGFlags", tserverGFlags);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeGflags)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseGFlagsUpgradeWithTrimParams() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "GFlags");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    JsonNode masterGFlags = Json.parse("[{ \"name\": \" master-flag \", \"value\": \" 123 \"}]");
    JsonNode tserverGFlags = Json.parse("[{ \"name\": \" tserver-flag \", \"value\": \" 456 \"}]");
    userIntentJson.set("masterGFlags", masterGFlags);
    userIntentJson.set("tserverGFlags", tserverGFlags);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.UpgradeGflags)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseGFlagsUpgradeWithInvalidParams() {
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "GFlags");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Test Universe");
    userIntentJson.set("masterGFlags", Json.parse("[\"gflag1\", \"123\"]"));
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Neither master nor tserver gflags changed.");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseGFlagsUpgradeWithSameGFlags() {
    Universe u = createUniverse(customer.getId());

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = ImmutableMap.of("master-flag", "123");
          userIntent.tserverGFlags = ImmutableMap.of("tserver-flag", "456");
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(u.getUniverseUUID(), updater);

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "GFlags")
            .put("upgradeOption", "Non-Rolling");
    ObjectNode userIntentJson = Json.newObject().put("universeName", u.getName());
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    JsonNode masterGFlags = Json.parse("[{ \"name\": \"master-flag\", \"value\": \"123\"}]");
    JsonNode tserverGFlags = Json.parse("[{ \"name\": \"tserver-flag\", \"value\": \"456\"}]");
    userIntentJson.set("masterGFlags", masterGFlags);
    userIntentJson.set("tserverGFlags", tserverGFlags);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Neither master nor tserver gflags changed");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseGFlagsUpgradeWithMissingGflags() {
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJsonMissingGFlags =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "GFlags");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJsonMissingGFlags.set("clusters", clustersJsonArray);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJsonMissingGFlags));

    assertBadRequest(result, "Neither master nor tserver gflags changed.");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseGFlagsUpgradeWithMalformedTServerFlags() {
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "GFlags");
    ObjectNode userIntentJson =
        Json.newObject().put("universeName", "Single UserUniverse").put("tserverGFlags", "abcd");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Neither master nor tserver gflags changed.");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseGFlagsUpgradeWithMalformedMasterGFlags() {
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "GFlags");
    ObjectNode userIntentJson =
        Json.newObject().put("universeName", "Single UserUniverse").put("masterGFlags", "abcd");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));

    assertBadRequest(result, "Neither master nor tserver gflags changed.");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseNonRollingGFlagsUpgrade() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.GFlagsUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "GFlags")
            .put("upgradeOption", "Non-Rolling");
    ObjectNode userIntentJson = Json.newObject().put("universeName", "Single UserUniverse");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    JsonNode masterGFlags = Json.parse("[{ \"name\": \"master-flag\", \"value\": \"123\"}]");
    JsonNode tserverGFlags = Json.parse("[{ \"name\": \"tserver-flag\", \"value\": \"456\"}]");
    userIntentJson.set("masterGFlags", masterGFlags);
    userIntentJson.set("tserverGFlags", tserverGFlags);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    ArgumentCaptor<UniverseTaskParams> taskParams =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), taskParams.capture());
    UpgradeParams taskParam = (UpgradeParams) taskParams.getValue();
    assertEquals(taskParam.upgradeOption, UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    assertEquals(taskParam.masterGFlags, ImmutableMap.of("master-flag", "123"));
    assertEquals(taskParam.tserverGFlags, ImmutableMap.of("tserver-flag", "456"));
    UniverseDefinitionTaskParams.UserIntent primaryClusterIntent =
        taskParam.getPrimaryCluster().userIntent;
    assertEquals(primaryClusterIntent.masterGFlags, taskParam.masterGFlags);
    assertEquals(primaryClusterIntent.tserverGFlags, taskParam.tserverGFlags);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testUniverseNonRollingSoftwareUpgrade() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.SoftwareUpgrade);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", "Software")
            .put("upgradeOption", "Non-Rolling")
            .put("ybSoftwareVersion", "new-version");
    ObjectNode userIntentJson =
        Json.newObject()
            .put("universeName", "Single UserUniverse")
            .put("ybSoftwareVersion", "new-version");
    ArrayNode clustersJsonArray =
        Json.newArray().add(Json.newObject().set("userIntent", userIntentJson));
    bodyJson.set("clusters", clustersJsonArray);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    ArgumentCaptor<UniverseTaskParams> taskParams =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());
    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), taskParams.capture());
    UpgradeParams taskParam = (UpgradeParams) taskParams.getValue();
    assertEquals(taskParam.upgradeOption, UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    assertEquals("new-version", taskParam.ybSoftwareVersion);
    assertAuditEntry(1, customer.getUuid());
  }

  private ObjectNode getResizeNodeValidPayload(Universe u, Provider p) {
    UUID fakeClusterUUID = UUID.randomUUID();
    ObjectNode bodyJson =
        Json.newObject()
            .put("universeUUID", u.getUniverseUUID().toString())
            .put("taskType", UpgradeTaskType.ResizeNode.toString())
            .put("upgradeOption", "Rolling");

    ObjectNode deviceInfoJson = Json.newObject().put("volumeSize", 600);

    ObjectNode userIntentJson = Json.newObject().put("instanceType", "test-instance-type");
    userIntentJson.set("deviceInfo", deviceInfoJson);

    if (p != null) {
      userIntentJson.put("providerType", p.getCode()).put("provider", p.getUuid().toString());
    }

    ObjectNode primaryCluster =
        Json.newObject().put("uuid", fakeClusterUUID.toString()).put("clusterType", "PRIMARY");
    primaryCluster.set("userIntent", userIntentJson);

    ArrayNode clustersJsonArray = Json.newArray().add(primaryCluster);
    bodyJson.set("clusters", clustersJsonArray);

    return bodyJson;
  }

  @Test
  public void testResizeNodeWithUnsupportedProvider() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.ResizeNode);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Universe u = createUniverse(customer.getId());

    Provider p = ModelFactory.newProvider(customer, Common.CloudType.azu);
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              universeDetails.getPrimaryCluster().userIntent;
          userIntent.providerType = Common.CloudType.azu;
          userIntent.provider = p.getUuid().toString();
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(u.getUniverseUUID(), updater);

    ObjectNode bodyJson = getResizeNodeValidPayload(u, p);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result =
        assertThrows(
                PlatformServiceException.class,
                () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson))
            .buildResult(fakeRequest);

    assertBadRequest(
        result, "Smart resizing is only supported for AWS / GCP, It is: " + p.getCode());

    assertNull(CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testResizeNodeValidParams() {
    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(true);
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.ResizeNode);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);

    Universe u = createUniverse(customer.getId());

    ObjectNode bodyJson = getResizeNodeValidPayload(u, null);

    String url =
        "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID() + "/upgrade";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);

    verify(mockCommissioner).submit(eq(TaskType.UpgradeUniverse), any(UniverseTaskParams.class));

    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    assertValue(json, "taskUUID", fakeTaskUUID.toString());

    CustomerTask th = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(th);
    assertThat(th.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(th.getTargetName(), allOf(notNullValue(), equalTo("Test Universe")));
    assertThat(th.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.ResizeNode)));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testExpandDiskSizeFailureInvalidSize() {
    Universe u = createUniverse(customer.getId());
    setupDiskUpdateTest(100, "c4.xlarge", PublicCloudConstants.StorageType.GP2, u);
    u = Universe.getOrBadRequest(u.getUniverseUUID());

    ObjectNode bodyJson = (ObjectNode) Json.toJson(u.getUniverseDetails());
    bodyJson.put("size", 50);

    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/disk_update";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Size can only be increased.");
  }

  @Test
  public void testExpandDiskSizeFailureInvalidStorage() {
    Universe u = createUniverse("Test universe", customer.getId(), Common.CloudType.gcp);
    setupDiskUpdateTest(100, "c4.xlarge", PublicCloudConstants.StorageType.Scratch, u);
    Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater(CloudType.gcp));
    u = Universe.getOrBadRequest(u.getUniverseUUID());

    ObjectNode bodyJson = (ObjectNode) Json.toJson(u.getUniverseDetails());
    bodyJson.put("size", 150);

    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/disk_update";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Cannot modify instance volumes.");
  }

  @Test
  public void testExpandDiskSizeFailureInvalidInstance() {
    Universe u = createUniverse(customer.getId());
    setupDiskUpdateTest(100, "i3.xlarge", PublicCloudConstants.StorageType.GP2, u);
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> {
          NodeDetails nodeDetails = new NodeDetails();
          nodeDetails.placementUuid = univ.getUniverseDetails().getPrimaryCluster().uuid;
          nodeDetails.cloudInfo = new CloudSpecificInfo();
          nodeDetails.cloudInfo.instance_type = "i3.xlarge";
          nodeDetails.azUuid = UUID.randomUUID();
          nodeDetails.nodeName = "q";
          univ.getUniverseDetails().nodeDetailsSet = Collections.singleton(nodeDetails);
        });
    u = Universe.getOrBadRequest(u.getUniverseUUID());

    ObjectNode bodyJson = (ObjectNode) Json.toJson(u.getUniverseDetails());
    bodyJson.put("size", 150);

    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/disk_update";
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson));
    assertBadRequest(result, "Cannot modify instance volumes.");
  }

  @Test
  public void testExpandDiskSizeSuccess() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.EditUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());
    setupDiskUpdateTest(100, "c4.xlarge", PublicCloudConstants.StorageType.GP2, u);
    u = Universe.getOrBadRequest(u.getUniverseUUID());

    ObjectNode bodyJson = (ObjectNode) Json.toJson(u.getUniverseDetails());
    bodyJson.put("size", 150);

    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + u.getUniverseUUID()
            + "/disk_update";
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    ArgumentCaptor<UniverseTaskParams> argCaptor =
        ArgumentCaptor.forClass(UniverseTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.UpdateDiskSize), argCaptor.capture());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testGetUpdateOptionsCreate() throws IOException {
    testGetAvailableOptions(x -> {}, CREATE, UniverseDefinitionTaskParams.UpdateOptions.UPDATE);
  }

  @Test
  public void testGetUpdateOptionsEditEmpty() throws IOException {
    testGetAvailableOptions(x -> {}, EDIT);
  }

  @Test
  public void testGetUpdateOptionsEditModifyTags() throws IOException {
    testGetAvailableOptions(
        u -> u.getPrimaryCluster().userIntent.instanceTags.put("aa", "bb"),
        EDIT,
        UniverseDefinitionTaskParams.UpdateOptions.UPDATE);
  }

  @Test
  public void testGetUpdateOptionsEditAddNode() throws IOException {
    testGetAvailableOptions(
        x -> {
          NodeDetails node = new NodeDetails();
          node.state = NodeState.ToBeAdded;
          node.placementUuid = x.nodeDetailsSet.iterator().next().placementUuid;
          x.nodeDetailsSet.add(node);
        },
        EDIT,
        UniverseDefinitionTaskParams.UpdateOptions.UPDATE);
  }

  @Test
  public void testGetUpdateOptionsEditSmartResizeNonRestart() throws IOException {
    testGetAvailableOptions(
        x -> x.getPrimaryCluster().userIntent.deviceInfo.volumeSize += 50,
        EDIT,
        UniverseDefinitionTaskParams.UpdateOptions.SMART_RESIZE_NON_RESTART);
  }

  @Test
  public void testGetUpdateOptionsEditSmartResizeAndFullMove() throws IOException {
    testGetAvailableOptions(
        u -> {
          List<NodeDetails> nodesToAdd = new ArrayList<>();
          u.nodeDetailsSet.forEach(
              node -> {
                node.state = NodeState.ToBeRemoved;
                NodeDetails newNode = new NodeDetails();
                newNode.state = NodeState.ToBeAdded;
                newNode.placementUuid = node.placementUuid;
                nodesToAdd.add(newNode);
              });
          u.nodeDetailsSet.addAll(nodesToAdd);
          u.getPrimaryCluster().userIntent.deviceInfo.volumeSize += 50;
          u.getPrimaryCluster().userIntent.instanceType = "c3.large";
        },
        EDIT,
        UniverseDefinitionTaskParams.UpdateOptions.SMART_RESIZE,
        UniverseDefinitionTaskParams.UpdateOptions.FULL_MOVE);
  }

  @Test
  public void testGetUpdateOptionsNoSRforK8s() throws IOException {
    testGetAvailableOptions(
        u -> {
          List<NodeDetails> nodesToAdd = new ArrayList<>();
          u.nodeDetailsSet.forEach(
              node -> {
                node.state = NodeState.ToBeRemoved;
                NodeDetails newNode = new NodeDetails();
                newNode.state = NodeState.ToBeAdded;
                newNode.placementUuid = node.placementUuid;
                nodesToAdd.add(newNode);
              });
          u.nodeDetailsSet.addAll(nodesToAdd);
          u.getPrimaryCluster().userIntent.deviceInfo.volumeSize += 50;
          u.getPrimaryCluster().userIntent.instanceType = "c3.large";
          u.getPrimaryCluster().userIntent.providerType = CloudType.kubernetes;
        },
        EDIT,
        UniverseDefinitionTaskParams.UpdateOptions.FULL_MOVE);
  }

  @Test
  public void testGetUpdateOptionsEditFullMove() throws IOException {
    testGetAvailableOptions(
        u -> {
          List<NodeDetails> nodesToAdd = new ArrayList<>();
          u.nodeDetailsSet.forEach(
              node -> {
                node.state = NodeState.ToBeRemoved;
                NodeDetails newNode = new NodeDetails();
                newNode.state = NodeState.ToBeAdded;
                newNode.placementUuid = node.placementUuid;
                nodesToAdd.add(newNode);
              });
          u.nodeDetailsSet.addAll(nodesToAdd);
          u.getPrimaryCluster().userIntent.deviceInfo.volumeSize += 50;
          // change placement => SR is not available.
          PlacementInfo.PlacementAZ az =
              u.getPrimaryCluster().placementInfo.azStream().findFirst().get();
          az.isAffinitized = !az.isAffinitized;
        },
        EDIT,
        UniverseDefinitionTaskParams.UpdateOptions.FULL_MOVE);
  }

  @Test
  public void testGetUpdateOptionsAffinitizedChanged() throws IOException {
    testGetAvailableOptions(
        x -> {
          PlacementInfo.PlacementAZ placementAZ =
              x.getPrimaryCluster().placementInfo.azStream().findFirst().get();
          placementAZ.isAffinitized = !placementAZ.isAffinitized;
        },
        EDIT,
        UniverseDefinitionTaskParams.UpdateOptions.UPDATE);
  }

  @Test
  public void testUniverseCreateWithDeletedRegionFail() {
    Provider p = ModelFactory.awsProvider(customer);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    Region r2 = Region.create(p, "region-2", "PlacementRegion 2", "default-image");
    AvailabilityZone.createOrThrow(r2, "az2-1", "PlacementAZ 1/2", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());

    ObjectNode bodyJson = Json.newObject();
    UserIntent userIntent = new UserIntent();
    userIntent.instanceType = i.getInstanceTypeCode();
    userIntent.universeName = "foo";
    userIntent.numNodes = 3;
    userIntent.provider = p.getUuid().toString();
    userIntent.regionList = Arrays.asList(r.getUuid(), r2.getUuid());

    DeviceInfo di = new DeviceInfo();
    di.storageType = PublicCloudConstants.StorageType.GP2;
    di.volumeSize = 100;
    di.numVolumes = 2;
    userIntent.deviceInfo = di;
    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY,
            userIntent,
            3,
            null,
            Collections.emptyList());
    UniverseDefinitionTaskParams.Cluster cluster =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.PRIMARY, userIntent);
    cluster.placementInfo = placementInfo;

    ArrayNode clustersJsonArray = Json.newArray().add(Json.toJson(cluster));
    bodyJson.set("clusters", clustersJsonArray);
    bodyJson.set("nodeDetailsSet", Json.newArray());
    r.disableRegionAndZones();
    Result result = assertPlatformException(() -> sendCreateRequest(bodyJson));
    assertErrorResponse(result, "Region region-1 is deleted");
    assertBadRequest(result, "");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseUpdateUnchangedFail() {
    Universe u = setUpUniverse();
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", url, authToken, Json.toJson(u.getUniverseDetails())));
    assertErrorResponse(result, "No changes that could be applied by EditUniverse");
  }

  @Test
  public void testUniverseUpdateChangeInstanceTypeFail() {
    Universe u = setUpUniverse();
    u.getUniverseDetails().getPrimaryCluster().userIntent.instanceType = "newInstanceType";
    u.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags = Map.of("foo", "bar");
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", url, authToken, Json.toJson(u.getUniverseDetails())));
    assertErrorResponse(result, "Cannot change instance type for existing node");
  }

  @Test
  public void testUniverseUpdateChangeVolumeSizeFail() {
    Universe u = setUpUniverse();
    u.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.volumeSize++;
    u.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags = Map.of("foo", "bar");
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", url, authToken, Json.toJson(u.getUniverseDetails())));
    assertErrorResponse(result, "Cannot change volume size for existing node");
  }

  @Test
  public void testUniverseUpdateChangeVolumeSizeK8sOk() {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CheckTServers);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = setUpUniverse(CloudType.kubernetes);
    u =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              universe.updateConfig(Map.of(Universe.HELM2_LEGACY, "true"));
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.7.9.0";
              universe.setUniverseDetails(universeDetails);
            });
    u = addNode(u);
    u.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.volumeSize++;
    JsonNode bodyJson = Json.toJson(u.getUniverseDetails());
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    Result result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);
    assertOk(result);
  }

  @Test
  public void testUniverseUpdateChangeNumVolumesFail() {
    Universe u = setUpUniverse();
    u = addNode(u);
    u.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.numVolumes++;
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    JsonNode bodyJson = Json.toJson(u.getUniverseDetails());
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertErrorResponse(result, "Cannot change num of volumes for existing node");
  }

  @Test
  public void testUniverseUpdateChangeTserverGFlagsFail() {
    Universe u = setUpUniverse();
    u = addNode(u);
    u.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(Collections.emptyMap(), Map.of("a", "b"));
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    JsonNode bodyJson = Json.toJson(u.getUniverseDetails());
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertErrorResponse(result, "Cannot change tserver gflags for existing node");
  }

  @Test
  public void testUniverseUpdateChangeMasterGFlagsFail() {
    Universe u = setUpUniverse();
    u = addNode(u);
    u.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(Map.of("a", "b"), Collections.emptyMap());
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    JsonNode bodyJson = Json.toJson(u.getUniverseDetails());
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertErrorResponse(result, "Cannot change master gflags for existing node");
  }

  @Test
  public void testUniverseUpdateChangeSystemdFail() {
    Universe u = setUpUniverse();
    u = addNode(u);
    u.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd =
        !u.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + u.getUniverseUUID();
    JsonNode bodyJson = Json.toJson(u.getUniverseDetails());
    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson));
    assertErrorResponse(result, "Cannot change systemd setting for existing node");
  }

  private Universe addNode(Universe u) {
    return Universe.saveDetails(
        u.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.numNodes++;
          PlacementInfoUtil.updateUniverseDefinition(
              universeDetails, customer.getId(), universeDetails.getPrimaryCluster().uuid, EDIT);
          universe.setUniverseDetails(universeDetails);
        });
  }

  private Universe setUpUniverse() {
    return setUpUniverse(CloudType.aws);
  }

  private Universe setUpUniverse(CloudType providerType) {
    Provider p =
        Provider.create(
            customer.getUuid(),
            providerType,
            providerType.name() + " Provider",
            new ProviderDetails());
    String accessKeyCode = "someKeyCode";
    AccessKey.create(p.getUuid(), accessKeyCode, new AccessKey.KeyInfo());
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ 3", "subnet-3");
    Universe u = createUniverse("Test universe", customer.getId(), providerType);
    InstanceType i =
        InstanceType.upsert(
            p.getUuid(), "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    Universe result =
        Universe.saveDetails(
            u.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
              userIntent.instanceType = i.getInstanceTypeCode();
              userIntent.regionList = Collections.singletonList(r.getUuid());
              userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
              PlacementInfoUtil.updateUniverseDefinition(
                  universeDetails,
                  customer.getId(),
                  universeDetails.getPrimaryCluster().uuid,
                  CREATE);
              AtomicInteger idx = new AtomicInteger();
              universeDetails.nodeDetailsSet.forEach(
                  node -> {
                    node.state = NodeState.Live;
                    node.nodeName =
                        UniverseDefinitionTaskBase.getNodeName(
                            universeDetails.getPrimaryCluster(),
                            "",
                            universeDetails.nodePrefix,
                            idx.incrementAndGet(),
                            node.cloudInfo.region,
                            node.cloudInfo.az);
                  });
              universe.setUniverseDetails(universeDetails);
            });
    return result;
  }

  private void testGetAvailableOptions(
      Consumer<UniverseDefinitionTaskParams> mutator,
      UniverseConfigureTaskParams.ClusterOperationType operationType,
      UniverseDefinitionTaskParams.UpdateOptions... expectedOptions)
      throws IOException {
    Universe u = createUniverse(customer.getId());
    Provider provider = awsProvider(customer);
    Region region = Region.create(provider, "test-region", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    // create default universe
    UserIntent userIntent = new UserIntent();
    userIntent.numNodes = 3;
    userIntent.provider = provider.getUuid().toString();
    userIntent.providerType = Common.CloudType.valueOf(provider.getCode());
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.instanceType = "c5.large";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.volumeSize = 100;
    Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent, true));
    u = Universe.getOrBadRequest(u.getUniverseUUID());
    UniverseDefinitionTaskParams udtp = u.getUniverseDetails();
    mutator.accept(udtp);
    udtp.setUniverseUUID(u.getUniverseUUID());

    ObjectNode bodyJson = (ObjectNode) Json.toJson(udtp);
    if (udtp.getPrimaryCluster().userIntent.instanceTags.size() > 0) {
      ArrayNode clusters = (ArrayNode) bodyJson.get("clusters");
      ObjectNode cluster = (ObjectNode) clusters.get(0);
      ArrayNode instanceTags = Json.newArray();
      udtp.getPrimaryCluster()
          .userIntent
          .instanceTags
          .entrySet()
          .forEach(
              entry -> {
                instanceTags.add(
                    Json.newObject().put("name", entry.getKey()).put("value", entry.getValue()));
              });
      ObjectNode userIntentNode = (ObjectNode) cluster.get("userIntent");
      userIntentNode.replace("instanceTags", instanceTags);
    }
    InstanceType.upsert(
        provider.getUuid(),
        udtp.getPrimaryCluster().userIntent.instanceType,
        10,
        5.5,
        new InstanceType.InstanceTypeDetails());
    bodyJson.put("currentClusterType", "PRIMARY");
    bodyJson.put("clusterOperation", operationType.name());

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/universe_update_options",
            authToken,
            bodyJson);
    assertEquals(
        Arrays.asList(expectedOptions).stream().map(o -> o.name()).collect(Collectors.toSet()),
        new HashSet<>(parseArrayOfStrings(result)));
  }

  private List<String> parseArrayOfStrings(Result result) throws IOException {
    ObjectMapper mapper = new ObjectMapper();
    String[] strings =
        mapper.readValue(
            contentAsString(result),
            TypeFactory.defaultInstance().constructArrayType(String.class));
    return Arrays.asList(strings);
  }

  private void setupDiskUpdateTest(
      int diskSize, String instanceType, PublicCloudConstants.StorageType storageType, Universe u) {

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.instanceType = instanceType;
          userIntent.providerType = storageType.getCloudType();
          DeviceInfo di = new DeviceInfo();
          di.volumeSize = diskSize;
          di.numVolumes = 2;
          di.storageType = storageType;
          userIntent.deviceInfo = di;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);
  }
}
