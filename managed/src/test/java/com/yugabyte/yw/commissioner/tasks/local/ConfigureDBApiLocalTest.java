// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ConfigureYCQLFormData;
import com.yugabyte.yw.forms.ConfigureYSQLFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class ConfigureDBApiLocalTest extends LocalProviderUniverseTestBase {

  private final String YCQL_PASSWORD = "Pass@123";
  private final String NON_EXISTANT_GFLAG_VALUE = "?????";
  private static final String CP_STABLE_VERSION = "2024.2.2.1-b6";
  private static final String CP_STABLE_VERSION_URL =
      "https://software.yugabyte.com/releases/2024.2.2.1/yugabyte-2024.2.2.1-b6-%s-%s.tar.gz";

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair(150, 180);
  }

  private Result configureYSQL(ConfigureYSQLFormData formData, UUID universeUUID) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/" + customer.getUuid() + "/universes/" + universeUUID + "/configure/ysql",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  private Result configureYCQL(ConfigureYCQLFormData formData, UUID universeUUID) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/" + customer.getUuid() + "/universes/" + universeUUID + "/configure/ycql",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  private void enableConnectionPoolingFlag() {}

  @Test
  public void testConfigureYSQL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    // Enable YSQL Auth for the universe.
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    // Enable YSQL Auth for the universe.
    ConfigureYSQLFormData formData = new ConfigureYSQLFormData();
    formData.enableYSQL = true;
    formData.enableYSQLAuth = true;
    formData.ysqlPassword = "Pass@123";

    Result result = configureYSQL(formData, universe.getUniverseUUID());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    initYSQL(universe, "", null, true);
    verifyYSQL(universe, false, YUGABYTE_DB, "", true);

    // Disable YSQL Auth for the universe.
    formData.enableYSQLAuth = false;

    result = configureYSQL(formData, universe.getUniverseUUID());
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe);

    int newYsqlServerRpcPort = 5678;
    int newYsqlServerHttpPort = 13001;
    formData.communicationPorts.ysqlServerRpcPort = newYsqlServerRpcPort;
    formData.communicationPorts.ysqlServerHttpPort = newYsqlServerHttpPort;
    formData.ysqlPassword = "";
    result = configureYSQL(formData, universe.getUniverseUUID());
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe);
    assertEquals(
        newYsqlServerRpcPort, universe.getUniverseDetails().communicationPorts.ysqlServerRpcPort);
    assertEquals(
        newYsqlServerHttpPort, universe.getUniverseDetails().communicationPorts.ysqlServerHttpPort);
    for (NodeDetails node : universe.getNodes()) {
      assertEquals(newYsqlServerRpcPort, node.ysqlServerRpcPort);
      assertEquals(newYsqlServerHttpPort, node.ysqlServerHttpPort);
    }
  }

  @Test
  public void testConfigureYCQL() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);

    // Enable YCQL Auth for the universe.
    ConfigureYCQLFormData formData = new ConfigureYCQLFormData();
    formData.enableYCQL = true;
    formData.enableYCQLAuth = true;
    formData.ycqlPassword = YCQL_PASSWORD;

    Result result = configureYCQL(formData, universe.getUniverseUUID());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    initYCQL(universe, true, YCQL_PASSWORD);
    verifyYCQL(universe, true, YCQL_PASSWORD);

    // Disable YCQL Auth for the universe.
    formData.enableYCQLAuth = false;

    result = configureYCQL(formData, universe.getUniverseUUID());
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYCQL(universe);

    int newYqlServerRpcPort = 9078;
    int newYqlServerHttpPort = 12001;
    formData.communicationPorts.yqlServerHttpPort = newYqlServerHttpPort;
    formData.communicationPorts.yqlServerRpcPort = newYqlServerRpcPort;
    formData.ycqlPassword = "";
    result = configureYCQL(formData, universe.getUniverseUUID());
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYCQL(universe);
    assertEquals(
        newYqlServerRpcPort, universe.getUniverseDetails().communicationPorts.yqlServerRpcPort);
    assertEquals(
        newYqlServerHttpPort, universe.getUniverseDetails().communicationPorts.yqlServerHttpPort);
    for (NodeDetails node : universe.getNodes()) {
      assertEquals(newYqlServerRpcPort, node.yqlServerRpcPort);
      assertEquals(newYqlServerHttpPort, node.yqlServerHttpPort);
    }
  }

  private void addRelease(String dbVersion, String dbVersionUrl) {
    String downloadURL = String.format(dbVersionUrl, os, arch);
    downloadAndSetUpYBSoftware(os, arch, downloadURL, dbVersion);
    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(dbVersion, getMetadataJson(dbVersion, false).get(dbVersion));
    YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
  }

  private void updateProviderDetailsForCreateUniverse(String dbVersion) {
    ybVersion = dbVersion;
    ybBinPath = deriveYBBinPath(dbVersion);
    LocalCloudInfo localCloudInfo = new LocalCloudInfo();
    localCloudInfo.setDataHomeDir(
        ((LocalCloudInfo) CloudInfoInterface.get(provider)).getDataHomeDir());
    localCloudInfo.setYugabyteBinDir(ybBinPath);
    localCloudInfo.setYbcBinDir(ybcBinPath);
    ProviderDetails.CloudInfo cloudInfo = new ProviderDetails.CloudInfo();
    cloudInfo.setLocal(localCloudInfo);
    ProviderDetails providerDetails = new ProviderDetails();
    providerDetails.setCloudInfo(cloudInfo);
    provider.setDetails(providerDetails);
    provider.update();
  }

  @Test
  public void testEnableConnectionPooling() throws InterruptedException {
    // Set the connection pooling flag to true.
    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        GlobalConfKeys.allowConnectionPooling.getKey(),
        "true",
        true);

    addRelease(CP_STABLE_VERSION, CP_STABLE_VERSION_URL);
    updateProviderDetailsForCreateUniverse(CP_STABLE_VERSION);

    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.enableYSQLAuth = true;
    userIntent.ysqlPassword = "Pass@123";
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);

    // Enable Connection Pooling for the universe.
    ConfigureYSQLFormData formData = new ConfigureYSQLFormData();
    formData.enableYSQL = true;
    formData.enableYSQLAuth = true;
    formData.enableConnectionPooling = true;
    formData.communicationPorts.internalYsqlServerRpcPort = 6434;
    formData.communicationPorts.ysqlServerRpcPort = 5434;
    formData.connectionPoolingGflags = constructConnectionPoolingGFlags(universe);

    Result result = configureYSQL(formData, universe.getUniverseUUID());
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableConnectionPooling);
    assertEquals(5434, universe.getUniverseDetails().communicationPorts.ysqlServerRpcPort);
    assertEquals(6434, universe.getUniverseDetails().communicationPorts.internalYsqlServerRpcPort);
    verifyYSQL(universe);
    verifyYSQL(
        universe,
        false,
        YUGABYTE_DB,
        "some_table" /* tableName */,
        true /* authEnabled */,
        true /* cpEnabled */);
    compareConnectionPoolingGFlags(universe);
  }

  private Map<UUID, SpecificGFlags> constructConnectionPoolingGFlags(Universe universe) {
    UUID clusterUUID1 = universe.getUniverseDetails().getPrimaryCluster().uuid;
    List<NodeDetails> universeNodes =
        universe.getNodes().stream()
            .sorted((node1, node2) -> node1.nodeName.compareTo(node2.nodeName))
            .collect(Collectors.toList());

    UUID azUUID1 = universeNodes.get(0).azUuid;
    UUID azUUID2 = universeNodes.get(1).azUuid;
    UUID azUUID3 = universeNodes.get(2).azUuid;
    Map<UUID, SpecificGFlags> connectionPoolingGflags = new HashMap<>();

    // Set the connection pooling gflags for the primary cluster.
    SpecificGFlags specificGFlagsCluster1 = new SpecificGFlags();
    specificGFlagsCluster1.setPerProcessFlags(new SpecificGFlags.PerProcessFlags());
    specificGFlagsCluster1
        .getPerProcessFlags()
        .value
        .put(
            UniverseTaskBase.ServerType.MASTER,
            Map.of("ysql_conn_mgr_max_client_connections", "10001"));
    specificGFlagsCluster1
        .getPerProcessFlags()
        .value
        .put(
            UniverseTaskBase.ServerType.TSERVER,
            Map.of("ysql_conn_mgr_max_client_connections", "10002"));

    specificGFlagsCluster1.setPerAZ(
        Map.of(
            azUUID2,
            new PerProcessFlags(
                Map.of(
                    UniverseTaskBase.ServerType.MASTER,
                        Map.of(
                            "ysql_conn_mgr_max_client_connections", "10003",
                            "ysql_conn_mgr_idle_time", "61"),
                    UniverseTaskBase.ServerType.TSERVER,
                        Map.of(
                            "ysql_conn_mgr_idle_time", "61",
                            "ysql_conn_mgr_max_client_connections", "10004"))),
            azUUID3,
            new PerProcessFlags(
                Map.of(
                    UniverseTaskBase.ServerType.MASTER,
                        Map.of(
                            "ysql_conn_mgr_max_client_connections", "10005",
                            "ysql_conn_mgr_idle_time", "62"),
                    UniverseTaskBase.ServerType.TSERVER,
                        Map.of(
                            "ysql_conn_mgr_idle_time", "62",
                            "ysql_conn_mgr_max_client_connections", "10006")))));

    connectionPoolingGflags.put(clusterUUID1, specificGFlagsCluster1);
    return connectionPoolingGflags;
  }

  private void compareConnectionPoolingGFlags(Universe universe) {
    List<NodeDetails> universeNodes =
        universe.getNodes().stream()
            .sorted((node1, node2) -> node1.nodeName.compareTo(node2.nodeName))
            .collect(Collectors.toList());
    Cluster cluster1 = universe.getUniverseDetails().getPrimaryCluster();

    // Validate the gflags on node 1.
    NodeDetails node1 = universeNodes.get(0);
    validateGflags(
        "ysql_conn_mgr_max_client_connections",
        "10001",
        getVarz(node1, universe, UniverseTaskBase.ServerType.MASTER),
        getDiskFlags(node1, universe, UniverseTaskBase.ServerType.MASTER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(UniverseTaskBase.ServerType.MASTER));
    validateGflags(
        "ysql_conn_mgr_idle_time",
        "60", /* Default value from DB side in memory */
        NON_EXISTANT_GFLAG_VALUE, /* We don't send anything to the disk from YBA */
        NON_EXISTANT_GFLAG_VALUE, /* We don't save anything to the universe details from YBA */
        getVarz(node1, universe, UniverseTaskBase.ServerType.MASTER),
        getDiskFlags(node1, universe, UniverseTaskBase.ServerType.MASTER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(UniverseTaskBase.ServerType.MASTER));
    validateGflags(
        "ysql_conn_mgr_max_client_connections",
        "10002",
        getVarz(node1, universe, UniverseTaskBase.ServerType.TSERVER),
        getDiskFlags(node1, universe, UniverseTaskBase.ServerType.TSERVER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(UniverseTaskBase.ServerType.TSERVER));
    validateGflags(
        "ysql_conn_mgr_idle_time",
        "60", /* Default value from DB side in memory */
        NON_EXISTANT_GFLAG_VALUE, /* We don't send anything to the disk from YBA */
        NON_EXISTANT_GFLAG_VALUE, /* We don't save anything to the universe details from YBA */
        getVarz(node1, universe, UniverseTaskBase.ServerType.TSERVER),
        getDiskFlags(node1, universe, UniverseTaskBase.ServerType.TSERVER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(UniverseTaskBase.ServerType.TSERVER));

    // Validate the gflags on node 2.
    NodeDetails node2 = universeNodes.get(1);
    validateGflags(
        "ysql_conn_mgr_max_client_connections",
        "10003",
        getVarz(node2, universe, UniverseTaskBase.ServerType.MASTER),
        getDiskFlags(node2, universe, UniverseTaskBase.ServerType.MASTER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerAZ()
            .get(node2.azUuid)
            .value
            .get(UniverseTaskBase.ServerType.MASTER));
    validateGflags(
        "ysql_conn_mgr_idle_time",
        "61",
        getVarz(node2, universe, UniverseTaskBase.ServerType.MASTER),
        getDiskFlags(node2, universe, UniverseTaskBase.ServerType.MASTER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerAZ()
            .get(node2.azUuid)
            .value
            .get(UniverseTaskBase.ServerType.MASTER));
    validateGflags(
        "ysql_conn_mgr_max_client_connections",
        "10004",
        getVarz(node2, universe, UniverseTaskBase.ServerType.TSERVER),
        getDiskFlags(node2, universe, UniverseTaskBase.ServerType.TSERVER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerAZ()
            .get(node2.azUuid)
            .value
            .get(UniverseTaskBase.ServerType.TSERVER));
    validateGflags(
        "ysql_conn_mgr_idle_time",
        "61",
        getVarz(node2, universe, UniverseTaskBase.ServerType.TSERVER),
        getDiskFlags(node2, universe, UniverseTaskBase.ServerType.TSERVER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerAZ()
            .get(node2.azUuid)
            .value
            .get(UniverseTaskBase.ServerType.TSERVER));

    // Validate the gflags on node 3.
    NodeDetails node3 = universeNodes.get(2);
    validateGflags(
        "ysql_conn_mgr_max_client_connections",
        "10005",
        getVarz(node3, universe, UniverseTaskBase.ServerType.MASTER),
        getDiskFlags(node3, universe, UniverseTaskBase.ServerType.MASTER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerAZ()
            .get(node3.azUuid)
            .value
            .get(UniverseTaskBase.ServerType.MASTER));
    validateGflags(
        "ysql_conn_mgr_idle_time",
        "62",
        getVarz(node3, universe, UniverseTaskBase.ServerType.MASTER),
        getDiskFlags(node3, universe, UniverseTaskBase.ServerType.MASTER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerAZ()
            .get(node3.azUuid)
            .value
            .get(UniverseTaskBase.ServerType.MASTER));
    validateGflags(
        "ysql_conn_mgr_max_client_connections",
        "10006",
        getVarz(node3, universe, UniverseTaskBase.ServerType.TSERVER),
        getDiskFlags(node3, universe, UniverseTaskBase.ServerType.TSERVER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerAZ()
            .get(node3.azUuid)
            .value
            .get(UniverseTaskBase.ServerType.TSERVER));
    validateGflags(
        "ysql_conn_mgr_idle_time",
        "62",
        getVarz(node3, universe, UniverseTaskBase.ServerType.TSERVER),
        getDiskFlags(node3, universe, UniverseTaskBase.ServerType.TSERVER),
        cluster1
            .userIntent
            .specificGFlags
            .getPerAZ()
            .get(node3.azUuid)
            .value
            .get(UniverseTaskBase.ServerType.TSERVER));
  }

  public void validateGflags(
      String gflagKey,
      String gflagExpectedValue,
      Map<String, String> gflagsInVarz,
      Map<String, String> gflagsOnDisk,
      Map<String, String> gflagsInUniverseDetails) {
    validateGflags(
        gflagKey,
        gflagExpectedValue,
        gflagExpectedValue,
        gflagExpectedValue,
        gflagsInVarz,
        gflagsOnDisk,
        gflagsInUniverseDetails);
  }

  public void validateGflags(
      String gflagKey,
      String gflagInVarzExpectedValue,
      String gflagOnDiskExpectedValue,
      String gflagInUniverseDetailsExpectedValue,
      Map<String, String> gflagsInVarz,
      Map<String, String> gflagsOnDisk,
      Map<String, String> gflagsInUniverseDetails) {
    String actual = gflagsInVarz.getOrDefault(gflagKey, NON_EXISTANT_GFLAG_VALUE);
    log.info("Actual gflag '{}', value in memory is '{}'.", gflagKey, actual);
    assertEquals("Compare in memory gflag " + gflagKey, gflagInVarzExpectedValue, actual);
    String onDisk = gflagsOnDisk.getOrDefault(gflagKey, NON_EXISTANT_GFLAG_VALUE);
    log.info("Actual gflag '{}', value on disk is '{}'.", gflagKey, onDisk);
    assertEquals("Compare on disk gflag " + gflagKey, gflagOnDiskExpectedValue, onDisk);
    String universeDetailsGflag =
        gflagsInUniverseDetails.getOrDefault(gflagKey, NON_EXISTANT_GFLAG_VALUE);
    log.info(
        "Actual gflag '{}', value in universe details is '{}'.", gflagKey, universeDetailsGflag);
    assertEquals(
        "Compare universe details gflag " + gflagKey,
        gflagInUniverseDetailsExpectedValue,
        universeDetailsGflag);
  }
}
