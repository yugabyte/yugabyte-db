// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType.DownloadingSoftware;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.*;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.Mockito.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateRootVolumes;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeTaskType;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.*;
import io.ebean.DB;
import io.ebean.SqlUpdate;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.*;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.naming.TestCaseName;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.ArgumentMatcher;
import org.mockito.InjectMocks;
import org.mockito.MockitoAnnotations;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.*;
import org.yb.master.CatalogEntityInfo.SysClusterConfigEntryPB;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
@Deprecated
public class UpgradeUniverseTest extends CommissionerBaseTest {
  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private static class CreateRootVolumesMatcher implements ArgumentMatcher<NodeTaskParams> {
    private UUID azUUID;

    public CreateRootVolumesMatcher(UUID azUUID) {
      this.azUUID = azUUID;
    }

    @Override
    public boolean matches(NodeTaskParams right) {
      if (!(right instanceof CreateRootVolumes.Params)) {
        return false;
      }

      return right.azUuid.equals(this.azUUID);
    }
  }

  @InjectMocks UpgradeUniverse upgradeUniverse;

  private YBClient mockClient;
  private Universe defaultUniverse;

  private Region region;
  private AvailabilityZone az1;
  private AvailabilityZone az2;
  private AvailabilityZone az3;

  private static final String CERT_1_CONTENTS =
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

  private static final String CERT_2_CONTENTS =
      "-----BEGIN CERTIFICATE-----\n"
          + "MIIDAjCCAeqgAwIBAgIGAXVCiJ4gMA0GCSqGSIb3DQEBCwUAMC4xFjAUBgNVBAMM\n"
          + "DXliLWFkbWluLXRlc3QxFDASBgNVBAoMC2V4YW1wbGUuY29tMB4XDTIwMTAxOTIw\n"
          + "MjQxMVoXDTIxMTAxOTIwMjQxMVowLjEWMBQGA1UEAwwNeWItYWRtaW4tdGVzdDEU\n"
          + "MBIGA1UECgwLZXhhbXBsZS5jb20wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK\n"
          + "AoIBAQCw8Mk+/MK0mP67ZEKL7cGyTzggau57MzTApveLfGF1Snln/Y7wGzgbskaM\n"
          + "0udz46es9HdaC/jT+PzMAAD9MCtAe5YYSL2+lmWT+WHdeJWF4XC/AVkjqj81N6OS\n"
          + "Uxio6ww0S9cAoDmF3gZlmkRwQcsruiZ1nVyQ7l+5CerQ02JwYBIYolUu/1qMruDD\n"
          + "pLsJ9LPWXPw2JsgYWyuEB5W1xEPDl6+QLTEVCFc9oN6wJOJgf0Y6OQODBrDRxddT\n"
          + "8h0mgJ6yzmkerR8VA0bknPQFeruWNJ/4PKDO9Itk5MmmYU/olvT5zMJ79K8RSvhN\n"
          + "+3gO8N7tcswaRP7HbEUmuVTtjFDlAgMBAAGjJjAkMBIGA1UdEwEB/wQIMAYBAf8C\n"
          + "AQEwDgYDVR0PAQH/BAQDAgLkMA0GCSqGSIb3DQEBCwUAA4IBAQCB10NLTyuqSD8/\n"
          + "HmbkdmH7TM1q0V/2qfrNQW86ywVKNHlKaZp9YlAtBcY5SJK37DJJH0yKM3GYk/ee\n"
          + "4aI566aj65gQn+wte9QfqzeotfVLQ4ZlhhOjDVRqSJVUdiW7yejHQLnqexdOpPQS\n"
          + "vwi73Fz0zGNxqnNjSNtka1rmduGwP0fiU3WKtHJiPL9CQFtRKdIlskKUlXg+WulM\n"
          + "x9yw5oa6xpsbCzSoS31fxYg71KAxVvKJYumdKV3ElGU/+AK1y4loyHv/kPp+59fF\n"
          + "9Q8gq/A6vGFjoZtVuuKUlasbMocle4Y9/nVxqIxWtc+aZ8mmP//J5oVXyzPs56dM\n"
          + "E1pTE1HS\n"
          + "-----END CERTIFICATE-----\n";

  @Override
  @Before
  public void setUp() {
    super.setUp();

    MockitoAnnotations.initMocks(this);
    upgradeUniverse.setUserTaskUUID(UUID.randomUUID());
    region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    az1 = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    az2 = AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    az3 = AvailabilityZone.createOrThrow(region, "az-3", "AZ 3", "subnet-3");
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
    customCertInfo.rootCertPath = "rootCertPath";
    customCertInfo.nodeCertPath = "nodeCertPath";
    customCertInfo.nodeKeyPath = "nodeKeyPath";
    createTempFile("upgrade_universe_test_ca.crt", CERT_1_CONTENTS);
    try {
      CertificateInfo.create(
          certUUID,
          defaultCustomer.getUuid(),
          "test",
          date,
          date,
          TestHelper.TMP_PATH + "/upgrade_universe_test_ca.crt",
          customCertInfo);
    } catch (IOException | NoSuchAlgorithmException e) {
    }

    // create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "old-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    defaultUniverse = createUniverse(defaultCustomer.getId(), certUUID);
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, false);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent, pi, true));

    // Setup mocks
    mockClient = mock(YBClient.class);
    try {
      when(mockClient.getMasterClusterConfig())
          .thenAnswer(
              i -> {
                SysClusterConfigEntryPB.Builder configBuilder =
                    SysClusterConfigEntryPB.newBuilder().setVersion(defaultUniverse.getVersion());
                return new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
              });
      UpgradeYsqlResponse mockUpgradeYsqlResponse = new UpgradeYsqlResponse(1000, "", null);
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockUpgradeYsqlResponse);
      IsInitDbDoneResponse mockIsInitDbDoneResponse =
          new IsInitDbDoneResponse(1000, "", true, true, null, null);
      when(mockClient.getIsInitDbDone()).thenReturn(mockIsInitDbDoneResponse);
    } catch (Exception ignored) {
      fail();
    }
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    when(mockClient.waitForServer(any(HostAndPort.class), anyLong())).thenReturn(true);
    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromString("10.0.0.2").withDefaultPort(7000));
    IsServerReadyResponse okReadyResp = new IsServerReadyResponse(0, "", null, 0, 0);
    try {
      when(mockClient.waitForMaster(any(HostAndPort.class), anyLong())).thenReturn(true);
      when(mockClient.isServerReady(any(HostAndPort.class), anyBoolean())).thenReturn(okReadyResp);
      when(mockClient.setFlag(any(HostAndPort.class), anyString(), anyString(), anyBoolean()))
          .thenReturn(true);
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
      when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
    } catch (Exception ignored) {
      fail();
    }
    ShellResponse dummyShellResponse = new ShellResponse();
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    ShellResponse successResponse = new ShellResponse();
    successResponse.message = "YSQL successfully upgraded to the latest version";
    when(mockNodeUniverseManager.runYbAdminCommand(any(), any(), any(), anyList(), anyLong()))
        .thenReturn(successResponse);

    setFollowerLagMock();
    setLeaderlessTabletsMock();
  }

  private TaskInfo submitTask(UpgradeUniverse.Params taskParams, UpgradeTaskType taskType) {
    return submitTask(taskParams, taskType, 2);
  }

  private TaskInfo submitTask(
      UpgradeUniverse.Params taskParams, UpgradeTaskType taskType, int expectedVersion) {
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.taskType = taskType;
    taskParams.expectedUniverseVersion = expectedVersion;
    // Need not sleep for default 3min in tests.
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;

    try {
      UUID taskUUID = commissioner.submit(TaskType.UpgradeUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private static final List<String> PROPERTY_KEYS = ImmutableList.of("processType", "taskSubType");

  private static final List<TaskType> NON_NODE_TASKS =
      ImmutableList.of(
          TaskType.LoadBalancerStateChange,
          TaskType.UpdateAndPersistGFlags,
          TaskType.UpdateSoftwareVersion,
          TaskType.UnivSetCertificate,
          TaskType.UniverseSetTlsParams,
          TaskType.UniverseUpdateSucceeded);

  private static final List<TaskType> GFLAGS_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.SetNodeState,
          TaskType.WaitForServer);

  private static final List<TaskType> GFLAGS_ROLLING_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.SetNodeState);

  private static final List<TaskType> CERTS_ROLLING_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.SetNodeState);

  private static final List<TaskType> CERTS_NON_ROLLING_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.SetNodeState,
          TaskType.WaitForServer);

  private static final List<TaskType> GFLAGS_NON_ROLLING_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeState,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeState);

  private static final List<TaskType> SOFTWARE_FULL_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.SetNodeState,
          TaskType.WaitForServer);

  private static final List<TaskType> SOFTWARE_ROLLING_UPGRADE_TASK_SEQUENCE_ACTIVE_ROLE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.SetNodeState);

  private static final List<TaskType> SOFTWARE_ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeState);

  private static final List<TaskType> ROLLING_RESTART_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.SetNodeState);

  private static final List<TaskType> VM_IMAGE_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.ReplaceRootVolume,
          TaskType.AnsibleSetupServer,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.UpdateNodeDetails);

  private static final List<TaskType> TOGGLE_TLS_ROLLING_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.SetNodeState);

  private static final List<TaskType> TOGGLE_TLS_NON_ROLLING_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.SetNodeState,
          TaskType.WaitForServer);

  private static final List<TaskType> TOGGLE_TLS_NON_RESTART_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.AnsibleConfigureServers,
          TaskType.SetNodeState,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeState);

  private static final List<TaskType> RESIZE_NODE_UPGRADE_TASK_SEQUENCE_NO_MASTER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.ChangeInstanceType,
          TaskType.UpdateNodeDetails,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState);

  private static final List<TaskType> RESIZE_NODE_UPGRADE_TASK_SEQUENCE_IS_MASTER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForMasterLeader,
          TaskType.ChangeMasterConfig,
          TaskType.ChangeInstanceType,
          TaskType.UpdateNodeDetails,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.ChangeMasterConfig,
          TaskType.CheckFollowerLag,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState);

  private int assertRollingRestartSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition, ServerType serverType, int startPosition) {
    int position = startPosition;
    List<TaskType> taskSequence = ROLLING_RESTART_TASK_SEQUENCE;
    List<Integer> nodeOrder = getRollingUpgradeNodeOrder(serverType);
    for (int nodeIdx : nodeOrder) {
      String nodeName = String.format("host-n%d", nodeIdx);
      for (TaskType type : taskSequence) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = tasks.get(0).getTaskType();

        assertEquals(1, tasks.size());
        assertEquals(type, taskType);
        if (!NON_NODE_TASKS.contains(taskType)) {
          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }
    return position;
  }

  private int assertCertsRotateSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      boolean isRollingUpgrade) {
    int position = startPosition;
    if (isRollingUpgrade) {
      List<TaskType> taskSequence = CERTS_ROLLING_UPGRADE_TASK_SEQUENCE;
      List<Integer> nodeOrder = getRollingUpgradeNodeOrder(serverType);
      for (int nodeIdx : nodeOrder) {
        String nodeName = String.format("host-n%d", nodeIdx);
        for (TaskType type : taskSequence) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = tasks.get(0).getTaskType();

          assertEquals(1, tasks.size());
          assertEquals(type, taskType);
          if (!NON_NODE_TASKS.contains(taskType)) {
            Map<String, Object> assertValues =
                new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
      }
    } else {
      for (TaskType type : CERTS_NON_ROLLING_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = assertTaskType(tasks, type);

        if (NON_NODE_TASKS.contains(taskType)) {
          assertEquals(1, tasks.size());
        } else {
          Map<String, Object> assertValues =
              ImmutableMap.of(
                  "nodeNames",
                  (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                  "nodeCount",
                  3);
          assertEquals(3, tasks.size());
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }

    return position;
  }

  private int assertSoftwareUpgradeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      boolean isRollingUpgrade,
      boolean activeRole) {
    int position = startPosition;
    if (isRollingUpgrade) {
      List<TaskType> taskSequence =
          activeRole
              ? SOFTWARE_ROLLING_UPGRADE_TASK_SEQUENCE_ACTIVE_ROLE
              : SOFTWARE_ROLLING_UPGRADE_TASK_SEQUENCE_INACTIVE_ROLE;
      List<Integer> nodeOrder = getRollingUpgradeNodeOrderForSWUpgrade(serverType, activeRole);
      for (int nodeIdx : nodeOrder) {
        String nodeName = String.format("host-n%d", nodeIdx);
        for (TaskType type : taskSequence) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = tasks.get(0).getTaskType();
          UserTaskDetails.SubTaskGroupType subTaskGroupType = tasks.get(0).getSubTaskGroupType();
          assertEquals(1, tasks.size());
          assertEquals(type, taskType);
          if (!NON_NODE_TASKS.contains(taskType)) {
            Map<String, Object> assertValues =
                new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));

            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              String version = "new-version";
              String taskSubType =
                  subTaskGroupType.equals(DownloadingSoftware) ? "Download" : "Install";
              assertValues.putAll(
                  ImmutableMap.of(
                      "ybSoftwareVersion", version,
                      "processType", serverType.toString(),
                      "taskSubType", taskSubType));
            }
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
      }
    } else {
      for (TaskType type : SOFTWARE_FULL_UPGRADE_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = assertTaskType(tasks, type);

        if (NON_NODE_TASKS.contains(taskType)) {
          assertEquals(1, tasks.size());
        } else {
          Map<String, Object> assertValues =
              new HashMap<>(
                  ImmutableMap.of(
                      "nodeNames",
                      (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                      "nodeCount",
                      3));
          if (taskType.equals(TaskType.AnsibleConfigureServers)) {
            String version = "new-version";
            assertValues.putAll(
                ImmutableMap.of(
                    "ybSoftwareVersion", version, "processType", serverType.toString()));
          }
          assertEquals(3, tasks.size());
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }
    return position;
  }

  private int assertGFlagsUpgradeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      UpgradeParams.UpgradeOption option) {
    return assertGFlagsUpgradeSequence(
        subTasksByPosition, serverType, startPosition, option, false);
  }

  private int assertGFlagsUpgradeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      UpgradeParams.UpgradeOption option,
      boolean isEdit) {
    return assertGFlagsUpgradeSequence(
        subTasksByPosition, serverType, startPosition, option, isEdit, false);
  }

  private int assertGFlagsUpgradeSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      UpgradeParams.UpgradeOption option,
      boolean isEdit,
      boolean isDelete) {
    int position = startPosition;
    switch (option) {
      case ROLLING_UPGRADE:
        List<TaskType> taskSequence = GFLAGS_ROLLING_UPGRADE_TASK_SEQUENCE;
        List<Integer> nodeOrder = getRollingUpgradeNodeOrder(serverType);
        for (int nodeIdx : nodeOrder) {
          String nodeName = String.format("host-n%d", nodeIdx);
          for (TaskType type : taskSequence) {
            List<TaskInfo> tasks = subTasksByPosition.get(position);
            TaskType taskType = tasks.get(0).getTaskType();
            assertEquals(1, tasks.size());
            assertEquals(type, taskType);
            if (!NON_NODE_TASKS.contains(taskType)) {
              Map<String, Object> assertValues =
                  new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));

              if (taskType.equals(TaskType.AnsibleConfigureServers)) {
                if (!isDelete) {
                  JsonNode gflagValue =
                      serverType.equals(MASTER)
                          ? Json.parse("{\"master-flag\":" + (isEdit ? "\"m2\"}" : "\"m1\"}"))
                          : Json.parse("{\"tserver-flag\":" + (isEdit ? "\"t2\"}" : "\"t1\"}"));
                  assertValues.putAll(ImmutableMap.of("gflags", gflagValue));
                }
              }
              assertNodeSubTask(tasks, assertValues);
            }
            position++;
          }
        }
        break;
      case NON_ROLLING_UPGRADE:
        for (TaskType value : GFLAGS_UPGRADE_TASK_SEQUENCE) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = assertTaskType(tasks, value);

          if (NON_NODE_TASKS.contains(taskType)) {
            assertEquals(1, tasks.size());
          } else {
            Map<String, Object> assertValues =
                new HashMap<>(
                    ImmutableMap.of(
                        "nodeNames",
                        (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                        "nodeCount",
                        3));
            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              if (!isDelete) {
                JsonNode gflagValue =
                    serverType.equals(MASTER)
                        ? Json.parse("{\"master-flag\":" + (isEdit ? "\"m2\"}" : "\"m1\"}"))
                        : Json.parse("{\"tserver-flag\":" + (isEdit ? "\"t2\"}" : "\"t1\"}"));
                assertValues.putAll(ImmutableMap.of("gflags", gflagValue));
              }
              assertValues.put("processType", serverType.toString());
            }
            assertEquals(3, tasks.size());
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
        break;

      case NON_RESTART_UPGRADE:
        for (TaskType type : GFLAGS_NON_ROLLING_UPGRADE_TASK_SEQUENCE) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = assertTaskType(tasks, type);

          if (NON_NODE_TASKS.contains(taskType)) {
            assertEquals(1, tasks.size());
          } else {
            Map<String, Object> assertValues =
                new HashMap<>(
                    ImmutableMap.of(
                        "nodeNames",
                        (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                        "nodeCount",
                        3));
            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              if (!isDelete) {
                JsonNode gflagValue =
                    serverType.equals(MASTER)
                        ? Json.parse("{\"master-flag\":" + (isEdit ? "\"m2\"}" : "\"m1\"}"))
                        : Json.parse("{\"tserver-flag\":" + (isEdit ? "\"t2\"}" : "\"t1\"}"));
                assertValues.putAll(ImmutableMap.of("gflags", gflagValue));
              }
            }
            assertEquals(3, tasks.size());
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
        break;
    }

    return position;
  }

  private int assertToggleTlsSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      UpgradeParams.UpgradeOption option) {
    int position = startPosition;
    if (option == UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
      List<TaskType> taskSequence = TOGGLE_TLS_ROLLING_UPGRADE_TASK_SEQUENCE;
      List<Integer> nodeOrder = getRollingUpgradeNodeOrder(serverType);

      for (int nodeIdx : nodeOrder) {
        String nodeName = String.format("host-n%d", nodeIdx);
        for (TaskType type : taskSequence) {
          List<TaskInfo> tasks = subTasksByPosition.get(position);
          TaskType taskType = tasks.get(0).getTaskType();
          assertEquals(1, tasks.size());
          assertEquals(type, taskType);
          if (!NON_NODE_TASKS.contains(taskType)) {
            Map<String, Object> assertValues =
                new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));
            if (taskType.equals(TaskType.AnsibleConfigureServers)) {
              assertValues.putAll(ImmutableMap.of("processType", serverType.toString()));
            }
            assertNodeSubTask(tasks, assertValues);
          }
          position++;
        }
      }
    } else if (option == UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE) {
      for (TaskType type : TOGGLE_TLS_NON_ROLLING_UPGRADE_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = assertTaskType(tasks, type);

        if (NON_NODE_TASKS.contains(taskType)) {
          assertEquals(1, tasks.size());
        } else {
          Map<String, Object> assertValues =
              new HashMap<>(
                  ImmutableMap.of(
                      "nodeNames",
                      (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                      "nodeCount",
                      3));
          if (taskType.equals(TaskType.AnsibleConfigureServers)) {
            assertValues.putAll(ImmutableMap.of("processType", serverType.toString()));
          }
          assertEquals(3, tasks.size());
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    } else {
      for (TaskType type : TOGGLE_TLS_NON_RESTART_UPGRADE_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position);
        TaskType taskType = assertTaskType(tasks, type);

        if (NON_NODE_TASKS.contains(taskType)) {
          assertEquals(1, tasks.size());
        } else {
          Map<String, Object> assertValues =
              new HashMap<>(
                  ImmutableMap.of(
                      "nodeNames",
                      (Object) ImmutableList.of("host-n1", "host-n2", "host-n3"),
                      "nodeCount",
                      3));
          if (taskType.equals(TaskType.AnsibleConfigureServers)) {
            assertValues.putAll(ImmutableMap.of("processType", serverType.toString()));
          }
          assertEquals(3, tasks.size());
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }

    return position;
  }

  public enum UpgradeType {
    ROLLING_UPGRADE,
    ROLLING_UPGRADE_MASTER_ONLY,
    ROLLING_UPGRADE_TSERVER_ONLY,
    FULL_UPGRADE,
    FULL_UPGRADE_MASTER_ONLY,
    FULL_UPGRADE_TSERVER_ONLY
  }

  private int assertRollingRestartCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition, int startPosition) {
    int position = startPosition;
    List<TaskType> commonNodeTasks =
        new ArrayList<>(
            ImmutableList.of(TaskType.LoadBalancerStateChange, TaskType.UniverseUpdateSucceeded));
    for (TaskType commonNodeTask : commonNodeTasks) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTask);
      position++;
    }
    return position;
  }

  private int assertGFlagsCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int startPosition,
      UpgradeType type,
      boolean isFinalStep) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();
    if (type.name().equals("ROLLING_UPGRADE")
        || type.name().equals("ROLLING_UPGRADE_TSERVER_ONLY")) {
      commonNodeTasks.add(TaskType.LoadBalancerStateChange);
    }

    if (isFinalStep) {
      commonNodeTasks.addAll(
          ImmutableList.of(TaskType.UpdateAndPersistGFlags, TaskType.UniverseUpdateSucceeded));
    }
    for (TaskType commonNodeTask : commonNodeTasks) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTask);
      position++;
    }
    return position;
  }

  private int assertSoftwareCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int startPosition,
      UpgradeType type,
      boolean isFinalStep) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();

    if (isFinalStep) {
      if (type.name().equals("ROLLING_UPGRADE")
          || type.name().equals("ROLLING_UPGRADE_TSERVER_ONLY")) {
        commonNodeTasks.add(TaskType.LoadBalancerStateChange);
      }

      commonNodeTasks.addAll(
          ImmutableList.of(
              TaskType.RunYsqlUpgrade,
              TaskType.UpdateSoftwareVersion,
              TaskType.UniverseUpdateSucceeded));
    }
    for (TaskType commonNodeTask : commonNodeTasks) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTask);
      position++;
    }
    return position;
  }

  private int assertCertsRotateCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int startPosition,
      UpgradeType type,
      boolean isFinalStep) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();

    if (isFinalStep) {
      if (type.name().equals("ROLLING_UPGRADE")) {
        commonNodeTasks.add(TaskType.LoadBalancerStateChange);
      }

      commonNodeTasks.addAll(
          ImmutableList.of(TaskType.UnivSetCertificate, TaskType.UniverseUpdateSucceeded));
    }
    for (TaskType commonNodeTask : commonNodeTasks) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTask);
      position++;
    }
    return position;
  }

  private int assertToggleTlsCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int startPosition,
      UpgradeParams.UpgradeOption upgradeOption,
      boolean isMetadataUpdateStep) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();
    if (upgradeOption == UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
      commonNodeTasks.add(TaskType.LoadBalancerStateChange);
    }
    if (isMetadataUpdateStep) {
      commonNodeTasks.addAll(ImmutableList.of(TaskType.UniverseSetTlsParams));
    }

    for (TaskType commonNodeTask : commonNodeTasks) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTask);
      position++;
    }

    return position;
  }

  private void assertNodeSubTask(List<TaskInfo> subTasks, Map<String, Object> assertValues) {
    List<String> nodeNames =
        subTasks.stream()
            .map(t -> t.getDetails().get("nodeName").textValue())
            .collect(Collectors.toList());
    int nodeCount = (int) assertValues.getOrDefault("nodeCount", 1);
    assertEquals(nodeCount, nodeNames.size());
    if (nodeCount == 1) {
      assertEquals(assertValues.get("nodeName"), nodeNames.get(0));
    } else {
      List assertNodeNames = (List) assertValues.get("nodeNames");
      assertTrue(nodeNames.containsAll(assertNodeNames));
      assertEquals(assertNodeNames.size(), nodeNames.size());
    }

    List<JsonNode> subTaskDetails =
        subTasks.stream().map(TaskInfo::getDetails).collect(Collectors.toList());
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
                          return data.isObject() ? data : data.textValue();
                        })
                    .collect(Collectors.toList());
            values.forEach(
                actualValue ->
                    assertEquals(
                        "Unexpected value for key " + expectedKey, expectedValue, actualValue));
          }
        });
  }

  public void testResizeNodeUpgrade(int rf, int numInvocations) {
    String intendedInstanceType = "c5.2xlarge";
    int intendedVolumeSize = 300;

    // Seed the database to have the intended type since we cannot mock static methods
    String updateQuery =
        "INSERT INTO instance_type ("
            + "provider_uuid, instance_type_code, active, num_cores, mem_size_gb,"
            + "instance_type_details )"
            + "VALUES ("
            + ":providerUUID, :typeCode, true, :numCores, :memSize, :details)";
    SqlUpdate update = DB.sqlUpdate(updateQuery);
    update.setParameter("providerUUID", defaultProvider.getUuid());
    update.setParameter("typeCode", intendedInstanceType);
    update.setParameter("numCores", 8);
    update.setParameter("memSize", 16);
    update.setParameter("details", "{\"volumeDetailsList\":[],\"tenancy\":\"Shared\"}");
    int modifiedCount = DB.getDefault().execute(update);
    assertEquals(1, modifiedCount);

    Region secondRegion = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone.createOrThrow(secondRegion, "az-4", "AZ 4", "subnet-4");

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster primaryCluster = universeDetails.getPrimaryCluster();
          UserIntent userIntent = primaryCluster.userIntent;
          userIntent.providerType = Common.CloudType.aws;
          userIntent.provider = defaultProvider.getUuid().toString();
          if (rf == 1) {
            userIntent.numNodes = 1;
            userIntent.replicationFactor = 1;
          }
          userIntent.instanceType = "c5.large";
          DeviceInfo deviceInfo = new DeviceInfo();
          deviceInfo.volumeSize = 250;
          deviceInfo.numVolumes = 1;
          userIntent.deviceInfo = deviceInfo;

          if (rf == 1) {
            for (NodeDetails nodeDetail : universeDetails.nodeDetailsSet) {
              if (nodeDetail.nodeIdx != 1) {
                nodeDetail.isMaster = false;
              }
              nodeDetail.cloudInfo.private_ip = "1.2.3." + nodeDetail.nodeIdx;
            }
          } else {
            for (int idx = userIntent.numNodes + 1; idx <= userIntent.numNodes + 2; idx++) {
              NodeDetails node = new NodeDetails();
              node.nodeIdx = idx;
              node.placementUuid = primaryCluster.uuid;
              node.nodeName = "host-n" + idx;
              node.isMaster = false;
              node.isTserver = true;
              node.cloudInfo = new CloudSpecificInfo();
              node.cloudInfo.instance_type = userIntent.getInstanceTypeForNode(node);
              node.cloudInfo.private_ip = "1.2.3." + idx;
              universeDetails.nodeDetailsSet.add(node);
            }
          }

          for (NodeDetails node : universeDetails.nodeDetailsSet) {
            node.nodeUuid = UUID.randomUUID();
          }
          userIntent.numNodes += 2;
          universe.setUniverseDetails(universeDetails);
        };
    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.ROLLING_UPGRADE;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.forceResizeNode = false;
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.volumeSize = intendedVolumeSize;
    taskParams.getPrimaryCluster().userIntent.deviceInfo = deviceInfo;
    taskParams.getPrimaryCluster().userIntent.instanceType = intendedInstanceType;
    TaskInfo taskInfo =
        submitTask(taskParams, UpgradeTaskType.ResizeNode, defaultUniverse.getVersion());
    verify(mockNodeManager, times(numInvocations)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    List<TaskInfo> changeDiskSize = subTasksByPosition.get(position++);
    assertEquals(
        changeDiskSize.size(),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes);
    assertTaskType(changeDiskSize, TaskType.InstanceActions);

    changeDiskSize.forEach(
        task -> {
          JsonNode details = task.getDetails();
          assertEquals(intendedVolumeSize, details.get("deviceInfo").get("volumeSize").asInt());
          assertEquals(1, details.get("deviceInfo").get("numVolumes").asInt());
          assertNotNull(details.get("instanceType"));
        });

    List<TaskInfo> persistChangeDiskSize = subTasksByPosition.get(position++);
    assertEquals(1, persistChangeDiskSize.size());
    assertTaskType(persistChangeDiskSize, TaskType.PersistResizeNode);

    // Find start position of each node's subtasks
    // nodeName to startPosition
    Map<String, Integer> nodeTasksStartPosition = new HashMap<>();
    String lastNode = null;
    for (int j = position; j < subTasksByPosition.size(); j++) {
      List<TaskInfo> tasks = subTasksByPosition.get(j);
      assertEquals(1, tasks.size());

      JsonNode nodeNameJson = tasks.get(0).getDetails().get("nodeName");
      if (nodeNameJson == null) {
        continue;
      }
      String nodeName = nodeNameJson.asText("NoNodeName");
      if (lastNode == null || !lastNode.equals(nodeName)) {
        nodeTasksStartPosition.put(nodeName, j);
        if (nodeTasksStartPosition.size()
            == defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.numNodes) {
          break;
        }
        lastNode = nodeName;
      }
    }

    for (NodeDetails node : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      String nodeName = node.nodeName;

      int tmpPosition = nodeTasksStartPosition.get(nodeName);

      if (node.isMaster) {
        for (int j = 0;
            j < RESIZE_NODE_UPGRADE_TASK_SEQUENCE_IS_MASTER.size()
                && tmpPosition < subTasksByPosition.size();
            j++) {
          if (rf == 1) {
            // Don't change master config for RF1
            if (RESIZE_NODE_UPGRADE_TASK_SEQUENCE_IS_MASTER.get(j) == TaskType.ChangeMasterConfig
                || RESIZE_NODE_UPGRADE_TASK_SEQUENCE_IS_MASTER.get(j) == TaskType.CheckFollowerLag
                || RESIZE_NODE_UPGRADE_TASK_SEQUENCE_IS_MASTER.get(j)
                    == TaskType.WaitForMasterLeader) {
              continue;
            }
          }

          List<TaskInfo> tasks = subTasksByPosition.get(tmpPosition++);
          assertEquals(1, tasks.size());
          TaskInfo task = tasks.get(0);
          TaskType taskType = task.getTaskType();
          assertEquals(RESIZE_NODE_UPGRADE_TASK_SEQUENCE_IS_MASTER.get(j), taskType);
          if (taskType == TaskType.ChangeInstanceType) {
            JsonNode details = task.getDetails();
            assertNotNull(details.get("instanceType").asText().equals(intendedInstanceType));
          }

          if (position < tmpPosition) {
            position = tmpPosition;
          }
        }
      } else {
        for (int j = 0;
            j < RESIZE_NODE_UPGRADE_TASK_SEQUENCE_NO_MASTER.size()
                && tmpPosition < subTasksByPosition.size();
            j++) {
          List<TaskInfo> tasks = subTasksByPosition.get(tmpPosition++);
          assertEquals(1, tasks.size());
          TaskInfo task = tasks.get(0);
          TaskType taskType = task.getTaskType();
          assertEquals(RESIZE_NODE_UPGRADE_TASK_SEQUENCE_NO_MASTER.get(j), taskType);
          if (taskType == TaskType.ChangeInstanceType) {
            JsonNode details = task.getDetails();
            assertNotNull(details.get("instanceType").asText().equals(intendedInstanceType));
          }

          if (position < tmpPosition) {
            position = tmpPosition;
          }
        }
      }
    }

    List<TaskInfo> persistChangeInstanceType = subTasksByPosition.get(position);
    assertEquals(1, persistChangeInstanceType.size());
    assertTaskType(persistChangeInstanceType, TaskType.PersistResizeNode);

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testResizeNodeUpgradeRF3() {
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    testResizeNodeUpgrade(3, 29);
  }

  @Test
  public void testResizeNodeUpgradeRF1() {
    testResizeNodeUpgrade(1, 15);
  }

  @Test
  public void testVMImageUpgrade() {
    Region secondRegion = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    AvailabilityZone az4 = AvailabilityZone.createOrThrow(secondRegion, "az-4", "AZ 4", "subnet-4");

    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          Cluster primaryCluster = universeDetails.getPrimaryCluster();
          UserIntent userIntent = primaryCluster.userIntent;
          userIntent.regionList = ImmutableList.of(region.getUuid(), secondRegion.getUuid());
          userIntent.provider = defaultProvider.getUuid().toString();

          PlacementInfo pi = primaryCluster.placementInfo;
          PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 1, 2, false);
          universe.setUniverseDetails(universeDetails);

          for (int idx = userIntent.numNodes + 1; idx <= userIntent.numNodes + 2; idx++) {
            NodeDetails node = new NodeDetails();
            node.nodeIdx = idx;
            node.placementUuid = primaryCluster.uuid;
            node.nodeName = "host-n" + idx;
            node.isMaster = true;
            node.isTserver = true;
            node.cloudInfo = new CloudSpecificInfo();
            node.cloudInfo.private_ip = "1.2.3." + idx;
            node.cloudInfo.az = az4.getCode();
            node.azUuid = az4.getUuid();
            universeDetails.nodeDetailsSet.add(node);
          }

          for (NodeDetails node : universeDetails.nodeDetailsSet) {
            node.nodeUuid = UUID.randomUUID();
          }

          userIntent.numNodes += 2;
        };

    defaultUniverse = Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.machineImages.put(region.getUuid(), "test-vm-image-1");
    taskParams.machineImages.put(secondRegion.getUuid(), "test-vm-image-2");

    // expect a CreateRootVolume for each AZ
    final int expectedRootVolumeCreationTasks = 4;

    Map<UUID, List<String>> createVolumeOutput =
        Arrays.asList(az1, az2, az3).stream()
            .collect(
                Collectors.toMap(
                    az -> az.getUuid(),
                    az ->
                        Collections.singletonList(String.format("root-volume-%s", az.getCode()))));
    // AZ 4 has 2 nodes so return 2 volumes here
    createVolumeOutput.put(az4.getUuid(), Arrays.asList("root-volume-4", "root-volume-5"));

    // Use output for verification and response is the raw string that parses into output.
    Map<UUID, String> createVolumeOutputResponse =
        Stream.of(az1, az2, az3)
            .collect(
                Collectors.toMap(
                    az -> az.getUuid(),
                    az ->
                        String.format(
                            "{\"boot_disks_per_zone\":[\"root-volume-%s\"], "
                                + "\"root_device_name\":\"/dev/sda1\"}",
                            az.getCode())));
    createVolumeOutputResponse.put(
        az4.getUuid(),
        "{\"boot_disks_per_zone\":[\"root-volume-4\", \"root-volume-5\"], "
            + "\"root_device_name\":\"/dev/sda1\"}");

    ObjectMapper om = new ObjectMapper();
    for (Map.Entry<UUID, String> e : createVolumeOutputResponse.entrySet()) {
      when(mockNodeManager.nodeCommand(
              eq(NodeCommandType.Create_Root_Volumes),
              argThat(new CreateRootVolumesMatcher(e.getKey()))))
          .thenReturn(ShellResponse.create(0, e.getValue()));
    }

    TaskInfo taskInfo =
        submitTask(
            taskParams, UpgradeTaskParams.UpgradeTaskType.VMImage, defaultUniverse.getVersion());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    List<TaskInfo> createRootVolumeTasks = subTasksByPosition.get(position++);
    assertTaskType(createRootVolumeTasks, TaskType.CreateRootVolumes);
    assertEquals(expectedRootVolumeCreationTasks, createRootVolumeTasks.size());

    createRootVolumeTasks.forEach(
        task -> {
          JsonNode details = task.getDetails();
          UUID azUuid = UUID.fromString(details.get("azUuid").asText());
          AvailabilityZone zone =
              AvailabilityZone.find.query().fetch("region").where().idEq(azUuid).findOne();
          String machineImage = details.get("machineImage").asText();
          assertEquals(taskParams.machineImages.get(zone.getRegion().getUuid()), machineImage);

          String azUUID = details.get("azUuid").asText();
          if (azUUID.equals(az4.getUuid().toString())) {
            assertEquals(2, details.get("numVolumes").asInt());
          }
        });

    List<Integer> nodeOrder = Arrays.asList(1, 3, 4, 5, 2);

    Map<UUID, Integer> replaceRootVolumeParams = new HashMap<>();

    for (int nodeIdx : nodeOrder) {
      String nodeName = String.format("host-n%d", nodeIdx);

      for (TaskType type : VM_IMAGE_UPGRADE_TASK_SEQUENCE) {
        List<TaskInfo> tasks = subTasksByPosition.get(position++);

        assertEquals(1, tasks.size());

        TaskInfo task = tasks.get(0);
        TaskType taskType = task.getTaskType();

        assertEquals(type, taskType);

        if (!NON_NODE_TASKS.contains(taskType)) {
          Map<String, Object> assertValues =
              new HashMap<>(ImmutableMap.of("nodeName", nodeName, "nodeCount", 1));

          assertNodeSubTask(tasks, assertValues);
        }

        if (taskType == TaskType.ReplaceRootVolume) {
          JsonNode details = task.getDetails();
          UUID az = UUID.fromString(details.get("azUuid").asText());
          replaceRootVolumeParams.compute(az, (k, v) -> v == null ? 1 : v + 1);
        }
      }
    }

    assertEquals(createVolumeOutput.keySet(), replaceRootVolumeParams.keySet());
    createVolumeOutput
        .entrySet()
        .forEach(
            e -> assertEquals(e.getValue().size(), (int) replaceRootVolumeParams.get(e.getKey())));
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeWithSameVersion() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.ybSoftwareVersion = "old-version";

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.Software);
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(Failure, taskInfo.getTaskState());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
    // In case of an exception, no task should be queued.
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  @Test
  public void testSoftwareUpgradeWithoutVersion() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.Software);
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(Failure, taskInfo.getTaskState());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
    // In case of an exception, no task should be queued.
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  @Test
  public void testSoftwareUpgrade() {
    // create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 5;
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "old-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 2, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 2, false);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(
                userIntent, "host", true /* setMasters */, false /* updateInProgress */, pi));

    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.ybSoftwareVersion = "new-version";
    TaskInfo taskInfo =
        submitTask(
            taskParams, UpgradeTaskParams.UpgradeTaskType.Software, defaultUniverse.getVersion());
    verify(mockNodeManager, times(33)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(5, downloadTasks.size());
    position = assertSoftwareUpgradeSequence(subTasksByPosition, MASTER, position, true, true);
    position = assertSoftwareUpgradeSequence(subTasksByPosition, MASTER, position, true, false);
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position =
        assertSoftwareCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, false);
    position = assertSoftwareUpgradeSequence(subTasksByPosition, TSERVER, position, true, true);
    assertSoftwareCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);
    assertEquals(74, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeWithReadReplica() {
    // Update default universe.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 5;
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "old-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 2, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 2, false);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(
                userIntent, "host", true /* setMasters */, false /* updateInProgress */, pi));

    pi = new PlacementInfo();
    AvailabilityZone az4 = AvailabilityZone.createOrThrow(region, "az-4", "AZ 4", "subnet-1");
    AvailabilityZone az5 = AvailabilityZone.createOrThrow(region, "az-5", "AZ 5", "subnet-2");
    AvailabilityZone az6 = AvailabilityZone.createOrThrow(region, "az-6", "AZ 6", "subnet-3");

    // Currently read replica zones are always affinitized.
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az5.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az6.getUuid(), pi, 1, 1, false);

    userIntent.numNodes = 3;

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.ybSoftwareVersion = "new-version";
    TaskInfo taskInfo =
        submitTask(
            taskParams, UpgradeTaskParams.UpgradeTaskType.Software, defaultUniverse.getVersion());
    verify(mockNodeManager, times(45)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(8, downloadTasks.size());
    position = assertSoftwareUpgradeSequence(subTasksByPosition, MASTER, position, true, true);
    position = assertSoftwareUpgradeSequence(subTasksByPosition, MASTER, position, true, false);
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position =
        assertSoftwareCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, false);
    position = assertSoftwareUpgradeSequence(subTasksByPosition, TSERVER, position, true, true);
    assertSoftwareCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);
    assertEquals(98, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareNonRollingUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.ybSoftwareVersion = "new-version";
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.Software);
    ArgumentCaptor<NodeTaskParams> commandParams = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, times(21)).nodeCommand(any(), commandParams.capture());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int position = 0;
    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(3, downloadTasks.size());
    position = assertSoftwareUpgradeSequence(subTasksByPosition, MASTER, position, false, true);
    position = assertSoftwareUpgradeSequence(subTasksByPosition, TSERVER, position, false, true);
    assertSoftwareCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true);
    assertEquals(13, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testGFlagsNonRollingUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition, MASTER, position, UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition, TSERVER, position, UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertGFlagsCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true);
    assertEquals(14, position);
  }

  @Test
  public void testGFlagsNonRollingMasterOnlyUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition, MASTER, position, UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertGFlagsCommonTasks(
            subTasksByPosition, position, UpgradeType.FULL_UPGRADE_MASTER_ONLY, true);
    assertEquals(8, position);
  }

  @Test
  public void testGFlagsNonRollingTServerOnlyUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition, TSERVER, position, UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    position =
        assertGFlagsCommonTasks(
            subTasksByPosition, position, UpgradeType.FULL_UPGRADE_TSERVER_ONLY, true);

    assertEquals(8, position);
  }

  @Test
  public void testGFlagsUpgradeWithMasterGFlags() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition, MASTER, position, UpgradeParams.UpgradeOption.ROLLING_UPGRADE);
    position =
        assertGFlagsCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(26, position);
  }

  @Test
  public void testGFlagsUpgradeWithTServerGFlags() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags);
    assertEquals(Success, taskInfo.getTaskState());
    ArgumentCaptor<NodeTaskParams> commandParams = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, times(9)).nodeCommand(any(), commandParams.capture());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition, TSERVER, position, UpgradeParams.UpgradeOption.ROLLING_UPGRADE);
    position =
        assertGFlagsCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, true);
    assertEquals(28, position);
  }

  @Test
  public void testGFlagsUpgrade() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition, MASTER, position, UpgradeParams.UpgradeOption.ROLLING_UPGRADE);
    position =
        assertGFlagsCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, false);
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition, TSERVER, position, UpgradeParams.UpgradeOption.ROLLING_UPGRADE);
    position =
        assertGFlagsCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);
    assertEquals(52, position);
  }

  @Test
  public void testGFlagsUpgradeWithEmptyFlags() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags);
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(Failure, taskInfo.getTaskState());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
    // In case of an exception, no task should be queued.
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  @Test
  public void testGFlagsUpgradeWithSameMasterFlags() {
    SysClusterConfigEntryPB.Builder configBuilder =
        SysClusterConfigEntryPB.newBuilder().setVersion(3);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
    } catch (Exception e) {
    }
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> masterFlags = ImmutableMap.of("master-flag", "m123");
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = masterFlags;
          userIntent.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = masterFlags;
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t2");

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags, 3);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition,
            TSERVER,
            position,
            UpgradeParams.UpgradeOption.ROLLING_UPGRADE,
            true);
    position =
        assertGFlagsCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY, true);
    assertEquals(28, position);
  }

  @Test
  public void testGFlagsUpgradeWithSameTserverFlags() {
    SysClusterConfigEntryPB.Builder configBuilder =
        SysClusterConfigEntryPB.newBuilder().setVersion(3);
    GetMasterClusterConfigResponse mockConfigResponse =
        new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
    } catch (Exception e) {
    }
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "m123");
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = ImmutableMap.of("master-flag", "m1");
          userIntent.tserverGFlags = tserverFlags;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m2");
    taskParams.tserverGFlags = tserverFlags;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags, 3);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int position = 0;
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition,
            MASTER,
            position,
            UpgradeParams.UpgradeOption.ROLLING_UPGRADE,
            true);
    position =
        assertGFlagsCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(26, position);
  }

  @Test
  public void testRemoveFlags() {
    for (ServerType serverType : ImmutableList.of(MASTER, TSERVER)) {
      if (serverType.equals(MASTER)) {
        SysClusterConfigEntryPB.Builder configBuilder =
            SysClusterConfigEntryPB.newBuilder().setVersion(3);
        GetMasterClusterConfigResponse mockConfigResponse =
            new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
        try {
          when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
        } catch (Exception e) {
        }
        when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
      } else if (serverType.equals(TSERVER)) {
        SysClusterConfigEntryPB.Builder configBuilder =
            SysClusterConfigEntryPB.newBuilder().setVersion(4);
        GetMasterClusterConfigResponse mockConfigResponse =
            new GetMasterClusterConfigResponse(1111, "", configBuilder.build(), null);
        try {
          when(mockClient.getMasterClusterConfig()).thenReturn(mockConfigResponse);
        } catch (Exception e) {
        }
        when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
      }
      // Simulate universe created with master flags and tserver flags.
      final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "t1");
      final Map<String, String> masterGFlags = ImmutableMap.of("master-flag", "m1");
      Universe.UniverseUpdater updater =
          universe -> {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
            userIntent.masterGFlags = masterGFlags;
            userIntent.tserverGFlags = tserverFlags;
            universe.setUniverseDetails(universeDetails);
          };
      Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

      UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
      // This is a delete operation on the master flags.
      if (serverType == MASTER) {
        taskParams.masterGFlags = new HashMap<>();
        taskParams.tserverGFlags = tserverFlags;
      } else {
        taskParams.masterGFlags = masterGFlags;
        taskParams.tserverGFlags = new HashMap<>();
      }

      int expectedVersion = serverType == MASTER ? 3 : 4;
      TaskInfo taskInfo =
          submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags, expectedVersion);
      assertEquals(Success, taskInfo.getTaskState());

      int numInvocations = serverType == MASTER ? 9 : 18;
      verify(mockNodeManager, times(numInvocations)).nodeCommand(any(), any());

      List<TaskInfo> subTasks = new ArrayList<>(taskInfo.getSubTasks());
      Map<Integer, List<TaskInfo>> subTasksByPosition =
          subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
      int position = 0;
      if (serverType != MASTER) {
        assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
      }
      position =
          assertGFlagsUpgradeSequence(
              subTasksByPosition,
              serverType,
              position,
              UpgradeParams.UpgradeOption.ROLLING_UPGRADE,
              true,
              true);
      position =
          assertGFlagsCommonTasks(
              subTasksByPosition,
              position,
              serverType == MASTER
                  ? UpgradeType.ROLLING_UPGRADE_MASTER_ONLY
                  : UpgradeType.ROLLING_UPGRADE_TSERVER_ONLY,
              true);
      assertEquals(serverType == MASTER ? 26 : 28, position);
    }
  }

  public void testGFlagsUpgradeNonRestart() throws Exception {
    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "t1");
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = ImmutableMap.of("master-flag", "m1");
          userIntent.tserverGFlags = tserverFlags;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    // SetFlagResponse response = new SetFlagResponse(0, "", null);
    when(mockClient.setFlag(any(), any(), any(), anyBoolean())).thenReturn(true);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m2");
    taskParams.tserverGFlags = tserverFlags;
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.GFlags, 3);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(3)).nodeCommand(any(), any());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    int position = 0;
    position =
        assertGFlagsUpgradeSequence(
            subTasksByPosition, MASTER, position, taskParams.upgradeOption, true);
    position =
        assertGFlagsCommonTasks(
            subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE_MASTER_ONLY, true);
    assertEquals(6, position);
  }

  @Test
  public void testRollingRestart() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.Restart);
    verify(mockNodeManager, times(12)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    position = assertRollingRestartSequence(subTasksByPosition, MASTER, position);
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position = assertRollingRestartSequence(subTasksByPosition, TSERVER, position);
    assertRollingRestartCommonTasks(subTasksByPosition, position);
    assertEquals(43, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testCertUpdateRolling() {
    defaultUniverse.save();
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
    customCertInfo.rootCertPath = "rootCertPath1";
    customCertInfo.nodeCertPath = "nodeCertPath1";
    customCertInfo.nodeKeyPath = "nodeKeyPath1";
    createTempFile("upgrade_universe_test_ca2.crt", CERT_1_CONTENTS);

    try {
      CertificateInfo.create(
          certUUID,
          defaultCustomer.getUuid(),
          "test2",
          date,
          date,
          TestHelper.TMP_PATH + "/upgrade_universe_test_ca2.crt",
          customCertInfo);
    } catch (IOException | NoSuchAlgorithmException e) {
    }
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.certUUID = certUUID;
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.Certs);
    verify(mockNodeManager, times(15)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(3, downloadTasks.size());
    position = assertCertsRotateSequence(subTasksByPosition, MASTER, position, true);
    assertTaskType(subTasksByPosition.get(position++), TaskType.LoadBalancerStateChange);
    position = assertCertsRotateSequence(subTasksByPosition, TSERVER, position, true);
    assertCertsRotateCommonTasks(subTasksByPosition, position, UpgradeType.ROLLING_UPGRADE, true);
    assertEquals(44, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testCertUpdateNonRolling() {
    defaultUniverse.save();
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
    customCertInfo.rootCertPath = "rootCertPath1";
    customCertInfo.nodeCertPath = "nodeCertPath1";
    customCertInfo.nodeKeyPath = "nodeKeyPath1";
    createTempFile("upgrade_universe_test_ca2.crt", CERT_1_CONTENTS);
    try {
      CertificateInfo.create(
          certUUID,
          defaultCustomer.getUuid(),
          "test2",
          date,
          date,
          TestHelper.TMP_PATH + "/upgrade_universe_test_ca2.crt",
          customCertInfo);
    } catch (IOException | NoSuchAlgorithmException e) {
    }
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.certUUID = certUUID;
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.Certs);
    verify(mockNodeManager, times(15)).nodeCommand(any(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    List<TaskInfo> downloadTasks = subTasksByPosition.get(position++);
    assertTaskType(downloadTasks, TaskType.AnsibleConfigureServers);
    assertEquals(3, downloadTasks.size());
    position = assertCertsRotateSequence(subTasksByPosition, MASTER, position, false);
    position = assertCertsRotateSequence(subTasksByPosition, TSERVER, position, false);
    assertCertsRotateCommonTasks(subTasksByPosition, position, UpgradeType.FULL_UPGRADE, true);
    assertEquals(11, position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testCertUpdateFailureDifferentCerts() {
    defaultUniverse.save();
    UUID certUUID = UUID.randomUUID();
    Date date = new Date();
    CertificateParams.CustomCertInfo customCertInfo = new CertificateParams.CustomCertInfo();
    customCertInfo.rootCertPath = "rootCertPath1";
    customCertInfo.nodeCertPath = "nodeCertPath1";
    customCertInfo.nodeKeyPath = "nodeKeyPath1";
    createTempFile("upgrade_universe_test_ca2.crt", CERT_2_CONTENTS);
    try {
      CertificateInfo.create(
          certUUID,
          defaultCustomer.getUuid(),
          "test2",
          date,
          date,
          TestHelper.TMP_PATH + "/upgrade_universe_test_ca2.crt",
          customCertInfo);
    } catch (IOException | NoSuchAlgorithmException e) {
    }
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.certUUID = certUUID;
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.Certs);
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(Failure, taskInfo.getTaskState());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
    // In case of an exception, no task should be queued.
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  private int getNodeToNodeChangeForToggleTls(boolean enableNodeToNodeEncrypt) {
    return defaultUniverse
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .enableNodeToNodeEncrypt
            != enableNodeToNodeEncrypt
        ? (enableNodeToNodeEncrypt ? 1 : -1)
        : 0;
  }

  private void prepareUniverseForToggleTls(
      boolean nodeToNode,
      boolean clientToNode,
      boolean rootAndClientRootCASame,
      UUID rootCA,
      UUID clientRootCA)
      throws IOException, NoSuchAlgorithmException {
    createTempFile("upgrade_universe_test_ca.crt", "test content");

    CertificateInfo.create(
        rootCA,
        defaultCustomer.getUuid(),
        "test1",
        new Date(),
        new Date(),
        "privateKey",
        TestHelper.TMP_PATH + "/upgrade_universe_test_ca.crt",
        CertConfigType.SelfSigned);

    if (!rootAndClientRootCASame && !rootCA.equals(clientRootCA)) {
      CertificateInfo.create(
          clientRootCA,
          defaultCustomer.getUuid(),
          "test1",
          new Date(),
          new Date(),
          "privateKey",
          TestHelper.TMP_PATH + "/upgrade_universe_test_ca.crt",
          CertConfigType.SelfSigned);
    }

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              PlacementInfo placementInfo = universeDetails.getPrimaryCluster().placementInfo;
              UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
              userIntent.enableNodeToNodeEncrypt = nodeToNode;
              userIntent.enableClientToNodeEncrypt = clientToNode;
              universeDetails.allowInsecure = true;
              universeDetails.rootAndClientRootCASame = rootAndClientRootCASame;
              if (nodeToNode || (rootAndClientRootCASame && clientToNode)) {
                universeDetails.rootCA = rootCA;
              }
              if (clientToNode) {
                universeDetails.setClientRootCA(clientRootCA);
              }
              if (nodeToNode || clientToNode) {
                universeDetails.allowInsecure = false;
              }
              universeDetails.upsertPrimaryCluster(userIntent, placementInfo);
              universe.setUniverseDetails(universeDetails);
            },
            false);
  }

  private UpgradeUniverse.Params getTaskParamsForToggleTls(
      boolean nodeToNode,
      boolean clientToNode,
      boolean rootAndClientRootCASame,
      UUID rootCA,
      UUID clientRootCA,
      UpgradeParams.UpgradeOption upgradeOption) {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.upgradeOption = upgradeOption;
    taskParams.enableNodeToNodeEncrypt = nodeToNode;
    taskParams.enableClientToNodeEncrypt = clientToNode;
    taskParams.rootAndClientRootCASame = rootAndClientRootCASame;
    taskParams.rootCA = rootCA;
    taskParams.setClientRootCA(clientRootCA);
    return taskParams;
  }

  private Pair<UpgradeParams.UpgradeOption, UpgradeParams.UpgradeOption>
      getUpgradeOptionsForToggleTls(int nodeToNodeChange, boolean isRolling) {
    if (isRolling) {
      return new Pair<>(
          nodeToNodeChange < 0
              ? UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE
              : UpgradeParams.UpgradeOption.ROLLING_UPGRADE,
          nodeToNodeChange > 0
              ? UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE
              : UpgradeParams.UpgradeOption.ROLLING_UPGRADE);
    } else {
      return new Pair<>(
          nodeToNodeChange < 0
              ? UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE
              : UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE,
          nodeToNodeChange > 0
              ? UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE
              : UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);
    }
  }

  private Pair<Integer, Integer> getExpectedValuesForToggleTls(UpgradeUniverse.Params taskParams) {
    int nodeToNodeChange = getNodeToNodeChangeForToggleTls(taskParams.enableNodeToNodeEncrypt);
    int expectedPosition = 1;
    int expectedNumberOfInvocations = 0;

    if (taskParams.enableNodeToNodeEncrypt || taskParams.enableClientToNodeEncrypt) {
      expectedPosition += 1;
      expectedNumberOfInvocations += 3;
    }

    if (taskParams.upgradeOption == UpgradeParams.UpgradeOption.ROLLING_UPGRADE) {
      if (nodeToNodeChange != 0) {
        expectedPosition += 58;
        expectedNumberOfInvocations += 24;
      } else {
        expectedPosition += 50;
        expectedNumberOfInvocations += 18;
      }
    } else {
      if (nodeToNodeChange != 0) {
        expectedPosition += 20;
        expectedNumberOfInvocations += 24;
      } else {
        expectedPosition += 12;
        expectedNumberOfInvocations += 18;
      }
    }

    return new Pair<>(expectedPosition, expectedNumberOfInvocations);
  }

  @Test
  public void testToggleTlsUpgradeInvalidUpgradeOption() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.enableNodeToNodeEncrypt = true;
    taskParams.upgradeOption = UpgradeParams.UpgradeOption.NON_RESTART_UPGRADE;
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.ToggleTls, -1);
    if (taskInfo == null) {
      fail();
    }

    defaultUniverse.refresh();
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(Failure, taskInfo.getTaskState());
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  @Test
  public void testToggleTlsUpgradeWithoutChangeInParams() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.ToggleTls, -1);
    if (taskInfo == null) {
      fail();
    }

    defaultUniverse.refresh();
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(Failure, taskInfo.getTaskState());
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  @Test
  public void testToggleTlsUpgradeWithoutRootCa() {
    UpgradeUniverse.Params taskParams = new UpgradeUniverse.Params();
    taskParams.enableNodeToNodeEncrypt = true;
    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.ToggleTls, -1);
    if (taskInfo == null) {
      fail();
    }

    defaultUniverse.refresh();
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertEquals(Failure, taskInfo.getTaskState());
    assertEquals(0, taskInfo.getSubTasks().size());
  }

  @Test
  @Parameters({
    //    "true, true, false, false, true, true",//both cannot be same, when rootCA is disabled
    "true, true, false, false, false, true",
    //    "true, false, false, false, true, true",//both cannot be same, when root CA is disabled
    "true, false, false, false, false, true",
    "false, true, false, true, true, true",
    "false, true, false, true, false, true",
    "false, false, false, true, true, true",
    "false, false, false, true, false, true",
    "true, true, false, true, false, true",
    "true, false, false, true, true, true",
    "false, true, false, false, false, true",
    //    "false, false, false, false, true, true",//both cannot be same when rootCA is disabled
    //    "true, true, true, false, true, true",//both cannot be same when rootCA is disabled
    "true, true, true, false, false, true",
    //    "true, false, true, false, true, true",//both cannot be same when rootCA is disabled
    "true, false, true, false, false, true",
    "false, true, true, true, true, true",
    "false, true, true, true, false, true",
    "false, false, true, true, true, true",
    "false, false, true, true, false, true",
    "true, true, true, true, false, true",
    "true, false, true, true, true, true",
    "false, true, true, false, false, true",
    //    "false, false, true, false, true, true",//both cannot be same when rootCA is disabled
    "true, true, true, false, true, false",
    "true, true, true, false, false, false",
    "true, false, true, false, true, false",
    "true, false, true, false, false, false",
    "false, true, true, true, true, false",
    "false, true, true, true, false, false",
    "false, false, true, true, true, false",
    "false, false, true, true, false, false",
    "true, true, true, true, false, false",
    "true, false, true, true, true, false",
    "false, true, true, false, false, false",
    "false, false, true, false, true, false",
    "true, true, false, false, true, false",
    "true, true, false, false, false, false",
    "true, false, false, false, true, false",
    "true, false, false, false, false, false",
    "false, true, false, true, true, false",
    "false, true, false, true, false, false",
    "false, false, false, true, true, false",
    "false, false, false, true, false, false",
    "true, true, false, true, false, false",
    "true, false, false, true, true, false",
    "false, true, false, false, false, false",
    "false, false, false, false, true, false"
  })
  @TestCaseName(
      "testToggleTlsNonRollingUpgradeWhen"
          + "CurrNodeToNode:{0}_CurrClientToNode:{1}_CurrRootAndClientRootCASame:{2}"
          + "_NodeToNode:{3}_ClientToNode:{4}_RootAndClientRootCASame:{5}")
  public void testToggleTlsNonRollingUpgrade(
      boolean currentNodeToNode,
      boolean currentClientToNode,
      boolean currRootAndClientRootCASame,
      boolean nodeToNode,
      boolean clientToNode,
      boolean rootAndClientRootCASame)
      throws IOException, NoSuchAlgorithmException {

    if (rootAndClientRootCASame && (!nodeToNode || !clientToNode)) {
      // bothCASame cannot be true when either one of nodeToNode or clientToNode is disabled
      rootAndClientRootCASame = false;
    }

    UUID rootCA = UUID.randomUUID();
    UUID clientRootCA = UUID.randomUUID();
    prepareUniverseForToggleTls(
        currentNodeToNode, currentClientToNode, currRootAndClientRootCASame, rootCA, clientRootCA);
    UpgradeUniverse.Params taskParams =
        getTaskParamsForToggleTls(
            nodeToNode,
            clientToNode,
            rootAndClientRootCASame,
            rootCA,
            clientRootCA,
            UpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE);

    int nodeToNodeChange = getNodeToNodeChangeForToggleTls(nodeToNode);
    Pair<UpgradeParams.UpgradeOption, UpgradeParams.UpgradeOption> upgrade =
        getUpgradeOptionsForToggleTls(nodeToNodeChange, false);
    Pair<Integer, Integer> expectedValues = getExpectedValuesForToggleTls(taskParams);

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.ToggleTls, -1);
    if (taskInfo == null) {
      fail();
    }

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    if (taskParams.enableNodeToNodeEncrypt || taskParams.enableClientToNodeEncrypt) {
      // Cert update tasks will be non rolling
      List<TaskInfo> certUpdateTasks = subTasksByPosition.get(position++);
      assertTaskType(certUpdateTasks, TaskType.AnsibleConfigureServers);
      assertEquals(3, certUpdateTasks.size());
    }
    // First round gflag update tasks
    position = assertToggleTlsSequence(subTasksByPosition, MASTER, position, upgrade.getFirst());
    position = assertToggleTlsSequence(subTasksByPosition, TSERVER, position, upgrade.getFirst());
    position = assertToggleTlsCommonTasks(subTasksByPosition, position, upgrade.getFirst(), true);
    if (nodeToNodeChange != 0) {
      // Second round gflag update tasks
      position = assertToggleTlsSequence(subTasksByPosition, MASTER, position, upgrade.getSecond());
      position =
          assertToggleTlsSequence(subTasksByPosition, TSERVER, position, upgrade.getSecond());
    }

    assertEquals((int) expectedValues.getFirst(), position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    if (nodeToNode || (rootAndClientRootCASame && clientToNode))
      assertEquals(taskParams.rootCA, universe.getUniverseDetails().rootCA);
    else assertNull(universe.getUniverseDetails().rootCA);
    if (clientToNode)
      assertEquals(taskParams.getClientRootCA(), universe.getUniverseDetails().getClientRootCA());
    else assertNull(universe.getUniverseDetails().getClientRootCA());
    assertEquals(
        nodeToNode,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt);
    assertEquals(
        clientToNode,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt);
    assertEquals(rootAndClientRootCASame, universe.getUniverseDetails().rootAndClientRootCASame);
    verify(mockNodeManager, times(expectedValues.getSecond())).nodeCommand(any(), any());
  }

  @Test
  @Parameters({
    //    "true, true, false, false, true, true",//both cannot be same when rootCA is disabled
    "true, true, false, false, false, true",
    //    "true, false, false, false, true, true",//both cannot be same when rootCA is disabled
    "true, false, false, false, false, true",
    "false, true, false, true, true, true",
    "false, true, false, true, false, true",
    "false, false, false, true, true, true",
    "false, false, false, true, false, true",
    "true, true, false, true, false, true",
    "true, false, false, true, true, true",
    "false, true, false, false, false, true",
    //    "false, false, false, false, true, true",//both cannot be same when rootCA is disabled
    //    "true, true, true, false, true, true",//both cannot be same when rootCA is disabled
    "true, true, true, false, false, true",
    //    "true, false, true, false, true, true",//both cannot be same when rootCA is disabled
    "true, false, true, false, false, true",
    "false, true, true, true, true, true",
    "false, true, true, true, false, true",
    "false, false, true, true, true, true",
    "false, false, true, true, false, true",
    "true, true, true, true, false, true",
    "true, false, true, true, true, true",
    "false, true, true, false, false, true",
    //    "false, false, true, false, true, true",//both cannot be same when rootCA is disabled
    "true, true, true, false, true, false",
    "true, true, true, false, false, false",
    "true, false, true, false, true, false",
    "true, false, true, false, false, false",
    "false, true, true, true, true, false",
    "false, true, true, true, false, false",
    "false, false, true, true, true, false",
    "false, false, true, true, false, false",
    "true, true, true, true, false, false",
    "true, false, true, true, true, false",
    "false, true, true, false, false, false",
    "false, false, true, false, true, false",
    "true, true, false, false, true, false",
    "true, true, false, false, false, false",
    "true, false, false, false, true, false",
    "true, false, false, false, false, false",
    "false, true, false, true, true, false",
    "false, true, false, true, false, false",
    "false, false, false, true, true, false",
    "false, false, false, true, false, false",
    "true, true, false, true, false, false",
    "true, false, false, true, true, false",
    "false, true, false, false, false, false",
    "false, false, false, false, true, false"
  })
  @TestCaseName(
      "testToggleTlsRollingUpgradeWhen"
          + "CurrNodeToNode:{0}_CurrClientToNode:{1}_CurrRootAndClientRootCASame:{2}"
          + "_NodeToNode:{3}_ClientToNode:{4}_RootAndClientRootCASame:{5}")
  public void testToggleTlsRollingUpgrade(
      boolean currentNodeToNode,
      boolean currentClientToNode,
      boolean currRootAndClientRootCASame,
      boolean nodeToNode,
      boolean clientToNode,
      boolean rootAndClientRootCASame)
      throws IOException, NoSuchAlgorithmException {

    if (rootAndClientRootCASame && (!nodeToNode || !clientToNode)) {
      // bothCASame cannot be true when either of nodeToNode or clientToNode are false
      rootAndClientRootCASame = false;
    }
    UUID rootCA = UUID.randomUUID();
    UUID clientRootCA = UUID.randomUUID();
    prepareUniverseForToggleTls(
        currentNodeToNode, currentClientToNode, currRootAndClientRootCASame, rootCA, clientRootCA);
    UpgradeUniverse.Params taskParams =
        getTaskParamsForToggleTls(
            nodeToNode,
            clientToNode,
            rootAndClientRootCASame,
            rootCA,
            rootAndClientRootCASame ? rootCA : clientRootCA,
            UpgradeParams.UpgradeOption.ROLLING_UPGRADE);

    int nodeToNodeChange = getNodeToNodeChangeForToggleTls(nodeToNode);
    Pair<UpgradeParams.UpgradeOption, UpgradeParams.UpgradeOption> upgrade =
        getUpgradeOptionsForToggleTls(nodeToNodeChange, true);
    Pair<Integer, Integer> expectedValues = getExpectedValuesForToggleTls(taskParams);

    TaskInfo taskInfo = submitTask(taskParams, UpgradeTaskParams.UpgradeTaskType.ToggleTls, -1);
    if (taskInfo == null) {
      fail();
    }

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    if (taskParams.enableNodeToNodeEncrypt || taskParams.enableClientToNodeEncrypt) {
      // Cert update tasks will be non rolling
      List<TaskInfo> certUpdateTasks = subTasksByPosition.get(position++);
      assertTaskType(certUpdateTasks, TaskType.AnsibleConfigureServers);
      assertEquals(3, certUpdateTasks.size());
    }
    // First round gflag update tasks
    position = assertToggleTlsSequence(subTasksByPosition, MASTER, position, upgrade.getFirst());
    position = assertToggleTlsCommonTasks(subTasksByPosition, position, upgrade.getFirst(), false);
    position = assertToggleTlsSequence(subTasksByPosition, TSERVER, position, upgrade.getFirst());
    position = assertToggleTlsCommonTasks(subTasksByPosition, position, upgrade.getFirst(), true);
    if (nodeToNodeChange != 0) {
      // Second round gflag update tasks
      position = assertToggleTlsSequence(subTasksByPosition, MASTER, position, upgrade.getSecond());
      position =
          assertToggleTlsCommonTasks(subTasksByPosition, position, upgrade.getSecond(), false);
      position =
          assertToggleTlsSequence(subTasksByPosition, TSERVER, position, upgrade.getSecond());
      position =
          assertToggleTlsCommonTasks(subTasksByPosition, position, upgrade.getSecond(), false);
    }

    assertEquals((int) expectedValues.getFirst(), position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    if (nodeToNode || (rootAndClientRootCASame && clientToNode))
      assertEquals(rootCA, universe.getUniverseDetails().rootCA);
    else assertNull(universe.getUniverseDetails().rootCA);
    if (clientToNode)
      assertEquals(taskParams.getClientRootCA(), universe.getUniverseDetails().getClientRootCA());
    else assertNull(universe.getUniverseDetails().getClientRootCA());
    assertEquals(
        nodeToNode,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt);
    assertEquals(
        clientToNode,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt);
    assertEquals(rootAndClientRootCASame, universe.getUniverseDetails().rootAndClientRootCASame);
    verify(mockNodeManager, times(expectedValues.getSecond())).nodeCommand(any(), any());
  }

  private List<Integer> getRollingUpgradeNodeOrder(ServerType serverType) {
    return serverType == MASTER
        ?
        // We need to check that the master leader is upgraded last.
        Arrays.asList(1, 3, 2)
        :
        // We need to check that isAffinitized zone node is upgraded getFirst().
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().isEmpty()
            ? Arrays.asList(2, 1, 3)
            :
            // Primary cluster getFirst(), then read replica.
            Arrays.asList(2, 1, 3, 6, 4, 5);
  }

  // Software upgrade has more nodes - 3 masters + 2 tservers only.
  private List<Integer> getRollingUpgradeNodeOrderForSWUpgrade(
      ServerType serverType, boolean activeRole) {
    return serverType == MASTER
        ?
        // We need to check that the master leader is upgraded last.
        (activeRole ? Arrays.asList(1, 3, 2) : Arrays.asList(4, 5))
        :
        // We need to check that isAffinitized zone node is upgraded getFirst().
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().isEmpty()
            ? Arrays.asList(3, 1, 2, 4, 5)
            :
            // Primary cluster getFirst(), then read replica.
            Arrays.asList(3, 1, 2, 4, 5, 8, 6, 7);
  }
}
