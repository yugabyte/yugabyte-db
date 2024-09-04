// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.libs.Json.newObject;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.yb.client.ChangeMasterClusterConfigResponse;
import org.yb.client.GetAutoFlagsConfigResponse;
import org.yb.client.GetLoadMovePercentResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.RollbackAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterClusterOuterClass.GetAutoFlagsConfigResponsePB;
import org.yb.master.MasterClusterOuterClass.PromoteAutoFlagsResponsePB;
import org.yb.master.MasterClusterOuterClass.RollbackAutoFlagsResponsePB;

@Slf4j
public abstract class UpgradeTaskTest extends CommissionerBaseTest {

  public enum UpgradeType {
    ROLLING_UPGRADE,
    ROLLING_UPGRADE_MASTER_ONLY,
    ROLLING_UPGRADE_TSERVER_ONLY,
    FULL_UPGRADE,
    FULL_UPGRADE_MASTER_ONLY,
    FULL_UPGRADE_TSERVER_ONLY
  }

  protected YBClient mockClient;
  protected Universe defaultUniverse;

  protected Region region;
  protected AvailabilityZone az1;
  protected AvailabilityZone az2;
  protected AvailabilityZone az3;

  protected Users defaultUser;

  protected Hook preUpgradeHook, postUpgradeHook, preNodeHook, postNodeHook;
  protected HookScope preNodeScope, postNodeScope, preUpgradeScope, postUpgradeScope;

  protected static final String CERT_CONTENTS =
      "-----BEGIN CERTIFICATE-----\n"
          + "MIIDEjCCAfqgAwIBAgIUEdzNoxkMLrZCku6H1jQ4pUgPtpQwDQYJKoZIhvcNAQEL\n"
          + "BQAwLzERMA8GA1UECgwIWXVnYWJ5dGUxGjAYBgNVBAMMEUNBIGZvciBZdWdhYnl0\n"
          + "ZURCMB4XDTIwMTIyMzA3MjU1MVoXDTIxMDEyMjA3MjU1MVowLzERMA8GA1UECgwI\n"
          + "WXVnYWJ5dGUxGjAYBgNVBAMMEUNBIGZvciBZdWdhYnl0ZURCMIIBIjANBgkqhkiG\n"
          + "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuLPcCR1KpVSs3B2515xNAR8ntfhOM5JjLl6Y\n"
          + "WjqoyRQ4wiOg5fGQpvjsearpIntr5t6uMevpzkDMYY4U21KbIW8Vvg/kXiASKMmM\n"
          + "W4ToH3Q0NfgLUNb5zJ8df3J2JZ5CgGSpipL8VPWsuSZvrqL7V77n+ndjMTUBNf57\n"
          + "eW4VjzYq+YQhyloOlXtlfWT6WaAsoaVOlbU0GK4dE2rdeo78p2mS2wQZSBDXovpp\n"
          + "0TX4zhT6LsJaRBZe49GE4SMkxz74alK1ezRvFcrPiNKr5NOYYG9DUUqFHWg47Bmw\n"
          + "KbiZ5dLdyxgWRDbanwl2hOMfExiJhHE7pqgr8XcizCiYuUzlDwIDAQABoyYwJDAO\n"
          + "BgNVHQ8BAf8EBAMCAuQwEgYDVR0TAQH/BAgwBgEB/wIBATANBgkqhkiG9w0BAQsF\n"
          + "AAOCAQEAVI3NTJVNX4XTcVAxXXGumqCbKu9CCLhXxaJb+J8YgmMQ+s9lpmBuC1eB\n"
          + "38YFdSEG9dKXZukdQcvpgf4ryehtvpmk03s/zxNXC5237faQQqejPX5nm3o35E3I\n"
          + "ZQqN3h+mzccPaUvCaIlvYBclUAt4VrVt/W66kLFPsfUqNKVxm3B56VaZuQL1ZTwG\n"
          + "mrIYBoaVT/SmEeIX9PNjlTpprDN/oE25fOkOxwHyI9ydVFkMCpBNRv+NisQN9c+R\n"
          + "/SBXfs+07aqFgrGTte6/I4VZ/6vz2cWMwZU+TUg/u0fc0Y9RzOuJrZBV2qPAtiEP\n"
          + "YvtLjmJF//b3rsty6NFIonSVgq6Nqw==\n"
          + "-----END CERTIFICATE-----\n";

  protected static final List<String> PROPERTY_KEYS =
      ImmutableList.of("processType", "taskSubType");

  protected static final List<TaskType> NON_NODE_TASKS =
      ImmutableList.of(
          TaskType.LoadBalancerStateChange,
          TaskType.UpdateAndPersistGFlags,
          TaskType.UpdateSoftwareVersion,
          TaskType.UnivSetCertificate,
          TaskType.UniverseSetTlsParams,
          TaskType.UniverseUpdateSucceeded,
          TaskType.WaitForMasterLeader,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.UpdateClusterUserIntent,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.WaitStartingFromTime);

  @Override
  @Before
  public void setUp() {
    super.setUp();

    // Create test region and Availability Zones
    region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    az1 = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    az2 = AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    az3 = AvailabilityZone.createOrThrow(region, "az-3", "AZ 3", "subnet-3");

    // Create test certificate
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
    customCertInfo.rootCertPath = "rootCertPath";
    customCertInfo.nodeCertPath = "nodeCertPath";
    customCertInfo.nodeKeyPath = "nodeKeyPath";
    createTempFile("upgrade_task_test_ca.crt", CERT_CONTENTS);
    try {
      CertificateInfo.create(
          certUUID,
          defaultCustomer.getUuid(),
          "test",
          date,
          date,
          TestHelper.TMP_PATH + "/upgrade_task_test_ca.crt",
          customCertInfo);
    } catch (IOException | NoSuchAlgorithmException ignored) {
    }

    // Create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.ybSoftwareVersion = "2.21.1.1-b1";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.providerType = Common.CloudType.valueOf(defaultProvider.getCode());
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.volumeSize = 100;
    userIntent.deviceInfo.numVolumes = 2;

    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId(), certUUID);

    PlacementInfo placementInfo = createPlacementInfo();
    userIntent.numNodes =
        placementInfo.cloudList.get(0).regionList.get(0).azList.stream()
            .mapToInt(p -> p.numNodesInAZ)
            .sum();

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, placementInfo, true));

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder configBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder().setVersion(1);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    ChangeMasterClusterConfigResponse mockMasterChangeConfigResponse =
        new ChangeMasterClusterConfigResponse(1112, "", null);
    GetLoadMovePercentResponse mockGetLoadMovePercentResponse =
        new GetLoadMovePercentResponse(0, "", 100.0, 0, 0, null);

    // Setup mocks
    mockClient = mock(YBClient.class);
    try {
      when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
      when(mockClient.waitForMaster(any(HostAndPort.class), anyLong())).thenReturn(true);
      when(mockClient.waitForServer(any(HostAndPort.class), anyLong())).thenReturn(true);
      when(mockClient.getLeaderMasterHostAndPort())
          .thenReturn(HostAndPort.fromString("10.0.0.2").withDefaultPort(11));
      IsServerReadyResponse okReadyResp = new IsServerReadyResponse(0, "", null, 0, 0);
      when(mockClient.isServerReady(any(HostAndPort.class), anyBoolean())).thenReturn(okReadyResp);
      GetAutoFlagsConfigResponse resp =
          new GetAutoFlagsConfigResponse(
              0, null, GetAutoFlagsConfigResponsePB.getDefaultInstance());
      lenient().when(mockClient.autoFlagsConfig()).thenReturn(resp);
      lenient().when(mockClient.ping(anyString(), anyInt())).thenReturn(true);
      lenient()
          .when(mockYBClient.getServerVersion(any(), anyString(), anyInt()))
          .thenReturn(Optional.of("2.17.0.0-b1"));
      lenient()
          .when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
          .thenReturn(
              new PromoteAutoFlagsResponse(
                  0, "uuid", PromoteAutoFlagsResponsePB.getDefaultInstance()));
      lenient()
          .when(mockClient.rollbackAutoFlags(anyInt()))
          .thenReturn(
              new RollbackAutoFlagsResponse(
                  0, "uuid", RollbackAutoFlagsResponsePB.getDefaultInstance()));
      lenient().when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
      lenient()
          .when(mockClient.changeMasterClusterConfig(any()))
          .thenReturn(mockMasterChangeConfigResponse);
      lenient()
          .when(mockClient.getLeaderBlacklistCompletion())
          .thenReturn(mockGetLoadMovePercentResponse);
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      autoFlagsPerServer.autoFlagDetails = new ArrayList<>();
      lenient()
          .when(mockGFlagsValidation.extractAutoFlags(anyString(), anyString()))
          .thenReturn(autoFlagsPerServer);
      lenient()
          .doAnswer(
              inv -> {
                ObjectNode res = newObject();
                res.put("response", "success");
                return res;
              })
          .when(mockYsqlQueryExecutor)
          .executeQueryInNodeShell(any(), any(), any(), anyBoolean(), anyBoolean(), anyInt());
    } catch (Exception ignored) {
      fail();
    }

    // Create dummy shell response
    ShellResponse dummyShellResponse = new ShellResponse();
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);

    defaultUser = ModelFactory.testUser(defaultCustomer);

    // Create hooks
    preNodeHook =
        Hook.create(
            defaultCustomer.getUuid(),
            "preNodeHook",
            Hook.ExecutionLang.Python,
            "HOOK\nTEXT\n",
            true,
            null);
    postNodeHook =
        Hook.create(
            defaultCustomer.getUuid(),
            "postNodeHook",
            Hook.ExecutionLang.Python,
            "HOOK\nTEXT\n",
            true,
            null);
    preUpgradeHook =
        Hook.create(
            defaultCustomer.getUuid(),
            "preUpgradeHook",
            Hook.ExecutionLang.Python,
            "HOOK\nTEXT\n",
            true,
            null);
    postUpgradeHook =
        Hook.create(
            defaultCustomer.getUuid(),
            "postUpgradeHook",
            Hook.ExecutionLang.Python,
            "HOOK\nTEXT\n",
            true,
            null);
  }

  protected PlacementInfo createPlacementInfo() {
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), placementInfo, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), placementInfo, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), placementInfo, 1, 1, false);
    return placementInfo;
  }

  protected TaskInfo submitTask(
      UpgradeTaskParams taskParams, TaskType taskType, Commissioner commissioner) {
    return submitTask(taskParams, taskType, commissioner, 2);
  }

  protected TaskInfo submitTask(
      UpgradeTaskParams taskParams,
      TaskType taskType,
      Commissioner commissioner,
      int expectedVersion) {
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = expectedVersion;
    // Need not sleep for default 3min in tests.
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    // Add the creating user
    taskParams.creatingUser = defaultUser;

    try {
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  protected List<Integer> getRollingUpgradeNodeOrder(ServerType serverType) {
    return serverType == ServerType.MASTER
        ?
        // We need to check that the master leader is upgraded last.
        Arrays.asList(1, 3, 2)
        :
        // We need to check that isAffinitized zone node is upgraded first.
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().isEmpty()
            ? Arrays.asList(2, 1, 3)
            :
            // Primary cluster first, then read replica.
            Arrays.asList(2, 1, 3, 6, 4, 5);
  }

  protected void assertNodeSubTask(List<TaskInfo> subTasks, Map<String, Object> assertValues) {
    List<String> nodeNames =
        subTasks.stream()
            .map(t -> t.getTaskParams().get("nodeName").textValue())
            .collect(Collectors.toList());
    int nodeCount = (int) assertValues.getOrDefault("nodeCount", 1);
    assertEquals(nodeCount, nodeNames.size());
    if (nodeCount == 1) {
      assertEquals(assertValues.get("nodeName"), nodeNames.get(0));
    } else {
      assertTrue(nodeNames.containsAll((List) assertValues.get("nodeNames")));
    }

    List<JsonNode> subTaskDetails =
        subTasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
    assertValues.forEach(
        (expectedKey, expectedValue) -> {
          if (!ImmutableList.of("nodeName", "nodeNames", "nodeCount").contains(expectedKey)) {
            List<Object> values =
                subTaskDetails.stream()
                    .map(
                        t -> {
                          JsonNode data =
                              PROPERTY_KEYS.contains(expectedKey)
                                  ? t.get("properties").get(expectedKey)
                                  : t.get(expectedKey);
                          return data.isObject()
                              ? data
                              : (data.isBoolean() ? data.booleanValue() : data.textValue());
                        })
                    .collect(Collectors.toList());
            values.forEach(
                actualValue ->
                    assertEquals(
                        "Unexpected value for key " + expectedKey, expectedValue, actualValue));
          }
        });
  }

  private void printTaskSequence(
      int startPosition,
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      List<TaskType> expectedTaskTypes,
      Map<Integer, Map<String, Object>> expectedParams,
      int failedPosition) {
    log.debug("Expected:");
    for (int i = 0; i < expectedTaskTypes.size(); i++) {
      log.debug(
          "#"
              + i
              + " "
              + expectedTaskTypes.get(i)
              + " "
              + expectedParams.getOrDefault(i, Collections.emptyMap()));
    }
    log.debug("Actual:");
    int maxPosition = subTasksByPosition.keySet().stream().max(Integer::compare).get();
    for (int i = 0; i < maxPosition - startPosition; i++) {
      int position = startPosition + i;
      String suff = "";
      if (position == failedPosition) {
        suff = "Failed!! ->";
      }
      String task;
      String taskParams;
      List<TaskInfo> taskInfos = subTasksByPosition.get(position);
      Set<String> keySet = expectedParams.getOrDefault(i, Collections.emptyMap()).keySet();
      if (taskInfos != null) {
        TaskInfo taskInfo = taskInfos.get(0);
        task = taskInfo.getTaskType().toString();
        taskParams = extractParams(taskInfo, keySet).toString();
      } else {
        task = "-";
        taskParams = "";
      }
      log.debug(suff + "#" + i + " " + task + " " + taskParams);
    }
    log.debug("------");
  }

  protected Map<String, Object> extractParams(TaskInfo task, Set<String> keys) {
    Map<String, Object> result = new HashMap<>();
    for (String key : keys) {
      if (key.equals("nodeNames") || key.equals("nodeCount")) {
        result.put(key, "?");
      } else {
        JsonNode details = task.getTaskParams();
        JsonNode data =
            PROPERTY_KEYS.contains(key) ? details.get("properties").get(key) : details.get(key);
        Object dataObj = null;
        if (data != null) {
          dataObj =
              data.isObject() ? data : (data.isBoolean() ? data.booleanValue() : data.textValue());
        }
        result.put(key, dataObj);
      }
    }
    return result;
  }

  // Configures default universe to have 5 nodes with RF=3.
  protected void updateDefaultUniverseTo5Nodes(boolean enableYSQL) {
    updateDefaultUniverseTo5Nodes(enableYSQL, "2.21.0.0-b1");
  }

  protected void updateDefaultUniverseTo5Nodes(boolean enableYSQL, String ybSoftwareVersion) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 5;
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = ybSoftwareVersion;
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.enableYSQL = enableYSQL;
    userIntent.provider = defaultProvider.getUuid().toString();

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 2, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 2, false);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(
                userIntent, "host", true /* setMasters */, false /* updateInProgress */, pi));
  }

  protected void attachHooks(String className) {
    // Create scopes
    preUpgradeScope =
        HookScope.create(defaultCustomer.getUuid(), TriggerType.valueOf("Pre" + className));
    postUpgradeScope =
        HookScope.create(defaultCustomer.getUuid(), TriggerType.valueOf("Post" + className));
    preNodeScope =
        HookScope.create(
            defaultCustomer.getUuid(), TriggerType.valueOf("Pre" + className + "NodeUpgrade"));
    postNodeScope =
        HookScope.create(
            defaultCustomer.getUuid(), TriggerType.valueOf("Post" + className + "NodeUpgrade"));

    // attack hooks
    preNodeScope.addHook(preNodeHook);
    postNodeScope.addHook(postNodeHook);
    preUpgradeScope.addHook(preUpgradeHook);
    postUpgradeScope.addHook(postUpgradeHook);
  }

  protected MockUpgrade initMockUpgrade(Class<? extends UpgradeTaskBase> upgradeClass) {
    MockUpgrade mockUpgrade =
        new MockUpgrade(mockBaseTaskDependencies, defaultUniverse, upgradeClass);

    return mockUpgrade;
  }

  protected TaskType[] getPrecheckTasks(boolean hasRollingRestarts) {
    List<TaskType> types = new ArrayList<>();
    if (hasRollingRestarts) {
      types.add(TaskType.CheckNodesAreSafeToTakeDown);
    }
    return types.toArray(new TaskType[0]);
  }
}
