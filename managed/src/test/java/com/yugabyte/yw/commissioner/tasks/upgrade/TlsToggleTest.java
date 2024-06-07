// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.naming.TestCaseName;
import org.apache.commons.lang3.RandomStringUtils;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.MockitoAnnotations;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class TlsToggleTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks private TlsToggle tlsToggle;

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_MASTER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.CheckFollowerLag,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.CheckNodesAreSafeToTakeDown,
          TaskType.AnsibleConfigureServers,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.CheckFollowerLag,
          TaskType.SetNodeState,
          TaskType.WaitStartingFromTime);

  private static final List<TaskType> NON_ROLLING_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState);

  private static final List<TaskType> NON_RESTART_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleConfigureServers,
          TaskType.SetFlagInMemory,
          TaskType.SetNodeState);

  @Override
  @Before
  public void setUp() {
    super.setUp();

    MockitoAnnotations.initMocks(this);

    try {
      when(mockClient.setFlag(any(HostAndPort.class), anyString(), anyString(), anyBoolean()))
          .thenReturn(true);
      setCheckNodesAreSafeToTakeDown(mockClient);
    } catch (Exception ignored) {
    }
    tlsToggle.setUserTaskUUID(UUID.randomUUID());

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
  }

  private TaskInfo submitTask(TlsToggleParams requestParams) {
    return submitTask(requestParams, TaskType.TlsToggle, commissioner, -1);
  }

  private int assertCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int startPosition,
      UpgradeOption upgradeOption,
      boolean isMetadataUpdateStep) {
    int position = startPosition;
    List<TaskType> commonNodeTasks = new ArrayList<>();

    if (isMetadataUpdateStep) {
      commonNodeTasks.addAll(ImmutableList.of(TaskType.UniverseSetTlsParams));
    }

    for (TaskType commonNodeTask : commonNodeTasks) {
      assertTaskType(subTasksByPosition.get(position), commonNodeTask);
      position++;
    }

    return position;
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      UpgradeOption option) {
    int position = startPosition;
    if (option == UpgradeOption.ROLLING_UPGRADE) {
      List<TaskType> taskSequence =
          serverType == MASTER
              ? ROLLING_UPGRADE_TASK_SEQUENCE_MASTER
              : ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER;
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
    } else if (option == UpgradeOption.NON_ROLLING_UPGRADE) {
      for (TaskType type : NON_ROLLING_UPGRADE_TASK_SEQUENCE) {
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
          // Remove ModifyBlackList task from tasks.
          tasks =
              tasks.stream()
                  .filter(t -> t.getTaskType() != TaskType.ModifyBlackList)
                  .collect(Collectors.toList());
          assertEquals(3, tasks.size());
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    } else {
      for (TaskType type : NON_RESTART_UPGRADE_TASK_SEQUENCE) {
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

  private void prepareUniverse(
      boolean nodeToNode,
      boolean clientToNode,
      boolean rootAndClientRootCASame,
      UUID rootCA,
      UUID clientRootCA)
      throws IOException, NoSuchAlgorithmException {
    createTempFile("tls_toggle_test_ca.crt", "test data");

    CertificateInfo.create(
        rootCA,
        defaultCustomer.getUuid(),
        "test1" + RandomStringUtils.randomAlphanumeric(8),
        new Date(),
        new Date(),
        "privateKey",
        TestHelper.TMP_PATH + "/tls_toggle_test_ca.crt",
        CertConfigType.SelfSigned);

    if (!clientRootCA.equals(rootCA)) {
      CertificateInfo.create(
          clientRootCA,
          defaultCustomer.getUuid(),
          "test1" + RandomStringUtils.randomAlphanumeric(8),
          new Date(),
          new Date(),
          "privateKey",
          TestHelper.TMP_PATH + "/tls_toggle_test_ca.crt",
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
              universeDetails.rootCA = null;
              if (EncryptionInTransitUtil.isRootCARequired(
                  nodeToNode, clientToNode, rootAndClientRootCASame)) {
                universeDetails.rootCA = rootCA;
              }
              universeDetails.setClientRootCA(null);
              if (EncryptionInTransitUtil.isClientRootCARequired(
                  nodeToNode, clientToNode, rootAndClientRootCASame)) {
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

  private TlsToggleParams getTaskParams(
      boolean nodeToNode,
      boolean clientToNode,
      boolean rootAndClientRootCASame,
      UUID rootCA,
      UUID clientRootCA,
      UpgradeOption upgradeOption) {
    TlsToggleParams taskParams = new TlsToggleParams();
    taskParams.upgradeOption = upgradeOption;
    taskParams.enableNodeToNodeEncrypt = nodeToNode;
    taskParams.enableClientToNodeEncrypt = clientToNode;
    taskParams.rootAndClientRootCASame = rootAndClientRootCASame;
    taskParams.rootCA = rootCA;
    if (clientToNode) {
      taskParams.setClientRootCA(rootAndClientRootCASame ? rootCA : clientRootCA);
    }
    return taskParams;
  }

  private Pair<UpgradeOption, UpgradeOption> getUpgradeOptions(
      int nodeToNodeChange, boolean isRolling) {
    if (isRolling) {
      return new Pair<>(
          nodeToNodeChange < 0 ? UpgradeOption.NON_RESTART_UPGRADE : UpgradeOption.ROLLING_UPGRADE,
          nodeToNodeChange > 0 ? UpgradeOption.NON_RESTART_UPGRADE : UpgradeOption.ROLLING_UPGRADE);
    } else {
      return new Pair<>(
          nodeToNodeChange < 0
              ? UpgradeOption.NON_RESTART_UPGRADE
              : UpgradeOption.NON_ROLLING_UPGRADE,
          nodeToNodeChange > 0
              ? UpgradeOption.NON_RESTART_UPGRADE
              : UpgradeOption.NON_ROLLING_UPGRADE);
    }
  }

  private Pair<Integer, Integer> getExpectedValues(TlsToggleParams taskParams) {
    int nodeToNodeChange = getNodeToNodeChange(taskParams.enableNodeToNodeEncrypt);
    int expectedPosition = 2;
    int expectedNumberOfInvocations = 0;

    if (taskParams.enableNodeToNodeEncrypt || taskParams.enableClientToNodeEncrypt) {
      expectedPosition += 1;
      expectedNumberOfInvocations += 3;
    }

    if (taskParams.upgradeOption == UpgradeOption.ROLLING_UPGRADE) {
      if (nodeToNodeChange != 0) {
        expectedPosition += 84;
        expectedNumberOfInvocations += 24;
      } else {
        expectedPosition += 76;
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

  private int getNodeToNodeChange(boolean enableNodeToNodeEncrypt) {
    return defaultUniverse
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .enableNodeToNodeEncrypt
            != enableNodeToNodeEncrypt
        ? (enableNodeToNodeEncrypt ? 1 : -1)
        : 0;
  }

  @Test
  public void testTlsToggleInvalidUpgradeOption() {
    TlsToggleParams taskParams = new TlsToggleParams();
    taskParams.enableNodeToNodeEncrypt = true;
    taskParams.upgradeOption = UpgradeOption.NON_RESTART_UPGRADE;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
  }

  @Test
  public void testTlsToggleWithoutChangeInParams() {
    TlsToggleParams taskParams = new TlsToggleParams();
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
  }

  @Test
  public void testTlsToggleWithoutRootCa() {
    TlsToggleParams taskParams = new TlsToggleParams();
    taskParams.enableNodeToNodeEncrypt = true;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
  }

  @Test
  @Parameters({
    "true, true, false, false, true, true",
    "true, true, false, false, false, true",
    "true, false, false, false, true, true",
    "true, false, false, false, false, true",
    //    "false, true, false, true, true, true",//clientRootCA cannot be changed
    "false, true, false, true, false, true", // both cannot be same, when clientToNode is disabled
    "false, false, false, true, true, true",
    "false, false, false, true, false, true",
    "true, true, false, true, false, true", // both cannot be same, when clientToNode is disabled
    "true, false, false, true, true, true",
    "false, true, false, false, false, true",
    "false, false, false, false, true, true",
    "true, true, true, false, true, true",
    "true, true, true, false, false, true",
    "true, false, true, false, true, true",
    "true, false, true, false, false, true",
    //    "false, true, true, true, true, true",//both cannot be same, when nodeToNode is disabled
    "false, true, true, true, false, true",
    "false, false, true, true, true, true",
    "false, false, true, true, false, true",
    "true, true, true, true, false, true",
    "true, false, true, true, true, true",
    "false, true, true, false, false, true",
    "false, false, true, false, true, true",
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
      "testTlsNonRollingUpgradeWhen"
          + "CurrNodeToNode:{0}_CurrClientToNode:{1}_CurrRootAndClientRootCASame:{2}"
          + "_NodeToNode:{3}_ClientToNode:{4}_RootAndClientRootCASame:{5}")
  public void testTlsNonRollingUpgrade(
      boolean currentNodeToNode,
      boolean currentClientToNode,
      boolean currRootAndClientRootCASame,
      boolean nodeToNode,
      boolean clientToNode,
      boolean rootAndClientRootCASame)
      throws IOException, NoSuchAlgorithmException {

    if (rootAndClientRootCASame && (!clientToNode || !nodeToNode)) {
      // when clientToNode is off, bothCASame flag cannot be true
      rootAndClientRootCASame = false;
    }

    UUID rootCA = UUID.randomUUID();
    UUID clientRootCA = currRootAndClientRootCASame ? rootCA : UUID.randomUUID();
    prepareUniverse(
        currentNodeToNode, currentClientToNode, currRootAndClientRootCASame, rootCA, clientRootCA);
    TlsToggleParams taskParams =
        getTaskParams(
            nodeToNode,
            clientToNode,
            rootAndClientRootCASame,
            rootCA,
            clientRootCA,
            UpgradeOption.NON_ROLLING_UPGRADE);

    int nodeToNodeChange = getNodeToNodeChange(nodeToNode);
    Pair<UpgradeOption, UpgradeOption> upgrade = getUpgradeOptions(nodeToNodeChange, false);
    Pair<Integer, Integer> expectedValues = getExpectedValues(taskParams);

    TaskInfo taskInfo = submitTask(taskParams);
    if (taskInfo == null) {
      fail();
    }

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    if (taskParams.enableNodeToNodeEncrypt || taskParams.enableClientToNodeEncrypt) {
      // Cert update tasks will be non rolling
      List<TaskInfo> certUpdateTasks = subTasksByPosition.get(position++);
      assertTaskType(certUpdateTasks, TaskType.AnsibleConfigureServers);
      assertEquals(3, certUpdateTasks.size());
    }
    // First round gflag update tasks
    position = assertSequence(subTasksByPosition, MASTER, position, upgrade.getFirst());
    position = assertSequence(subTasksByPosition, TSERVER, position, upgrade.getFirst());
    position = assertCommonTasks(subTasksByPosition, position, upgrade.getFirst(), true);
    if (nodeToNodeChange != 0) {
      // Second round gflag update tasks
      position = assertSequence(subTasksByPosition, MASTER, position, upgrade.getSecond());
      position = assertSequence(subTasksByPosition, TSERVER, position, upgrade.getSecond());
    }

    assertEquals((int) expectedValues.getFirst(), position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(expectedValues.getSecond())).nodeCommand(any(), any());

    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    if (EncryptionInTransitUtil.isRootCARequired(
        nodeToNode, clientToNode, rootAndClientRootCASame)) {
      assertEquals(taskParams.rootCA, universe.getUniverseDetails().rootCA);
    } else {
      assertNull(universe.getUniverseDetails().rootCA);
    }
    if (EncryptionInTransitUtil.isClientRootCARequired(
        nodeToNode, clientToNode, rootAndClientRootCASame)) {
      assertEquals(taskParams.getClientRootCA(), universe.getUniverseDetails().getClientRootCA());
    } else {
      assertNull(universe.getUniverseDetails().getClientRootCA());
    }
    assertEquals(
        nodeToNode,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt);
    assertEquals(
        clientToNode,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt);
    assertEquals(rootAndClientRootCASame, universe.getUniverseDetails().rootAndClientRootCASame);
  }

  // Due to a bug, temporarily disable rolling upgrade for TLS toggle UTs.
  // @Test
  @Parameters({
    "true, true, false, false, true, true",
    "true, true, false, false, false, true",
    "true, false, false, false, true, true",
    "true, false, false, false, false, true",
    //    "false, true, false, true, true, true",//client CA cannot be changed
    "false, true, false, true, false, true",
    "false, false, false, true, true, true",
    "false, false, false, true, false, true",
    "true, true, false, true, false, true",
    "true, false, false, true, true, true",
    "false, true, false, false, false, true",
    "false, false, false, false, true, true",
    "true, true, true, false, true, true",
    "true, true, true, false, false, true",
    "true, false, true, false, true, true",
    "true, false, true, false, false, true",
    //    "false, true, true, true, true, true",//client CA cannot be changed
    "false, true, true, true, false, true",
    "false, false, true, true, true, true",
    "false, false, true, true, false, true",
    "true, true, true, true, false, true",
    "true, false, true, true, true, true",
    "false, true, true, false, false, true",
    "false, false, true, false, true, true",
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
      "testTlsRollingUpgradeWhen"
          + "CurrNodeToNode:{0}_CurrClientToNode:{1}_CurrRootAndClientRootCASame:{2}"
          + "_NodeToNode:{3}_ClientToNode:{4}_RootAndClientRootCASame:{5}")
  public void testTlsRollingUpgrade(
      boolean currentNodeToNode,
      boolean currentClientToNode,
      boolean currRootAndClientRootCASame,
      boolean nodeToNode,
      boolean clientToNode,
      boolean rootAndClientRootCASame)
      throws IOException, NoSuchAlgorithmException {

    if (rootAndClientRootCASame && (!clientToNode || !nodeToNode)) {
      // when clientToNode is off, bothCASame flag cannot be true
      rootAndClientRootCASame = false;
    }

    UUID rootCA = UUID.randomUUID();
    UUID clientRootCA = currRootAndClientRootCASame ? rootCA : UUID.randomUUID();
    prepareUniverse(
        currentNodeToNode, currentClientToNode, currRootAndClientRootCASame, rootCA, clientRootCA);
    TlsToggleParams taskParams =
        getTaskParams(
            nodeToNode,
            clientToNode,
            rootAndClientRootCASame,
            rootCA,
            clientRootCA,
            UpgradeOption.ROLLING_UPGRADE);

    int nodeToNodeChange = getNodeToNodeChange(nodeToNode);
    Pair<UpgradeOption, UpgradeOption> upgrade = getUpgradeOptions(nodeToNodeChange, true);
    Pair<Integer, Integer> expectedValues = getExpectedValues(taskParams);

    TaskInfo taskInfo = submitTask(taskParams);
    if (taskInfo == null) {
      fail();
    }

    UniverseDefinitionTaskParams universeDetails = defaultUniverse.getUniverseDetails();
    // failure cases
    boolean clientToNodeTurnedOn = clientToNode && !currentClientToNode;
    boolean clientRootCAChanged =
        clientToNode && !clientRootCA.equals(taskParams.getClientRootCA());
    boolean nodeToNodeTurnedOn = nodeToNode && !currentNodeToNode;
    boolean rootCAChanged = nodeToNode && !rootCA.equals(taskParams.rootCA);

    // CA changed, but not because of 'turning on' TLS is a failure case
    if ((clientRootCAChanged && !clientToNodeTurnedOn) || (rootCAChanged && !nodeToNodeTurnedOn)) {
      assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
      return;
    }

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    if (taskParams.enableNodeToNodeEncrypt || taskParams.enableClientToNodeEncrypt) {
      // Cert update tasks will be non rolling
      List<TaskInfo> certUpdateTasks = subTasksByPosition.get(position++);
      assertTaskType(certUpdateTasks, TaskType.AnsibleConfigureServers);
      assertEquals(3, certUpdateTasks.size());
    }
    // First round gflag update tasks
    position = assertSequence(subTasksByPosition, MASTER, position, upgrade.getFirst());
    // Assert for ModifyBlackListTask before TSERVER rolling upgrade.
    if (upgrade.getFirst() == UpgradeOption.ROLLING_UPGRADE) {
      assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
    }
    position = assertSequence(subTasksByPosition, TSERVER, position, upgrade.getFirst());
    position = assertCommonTasks(subTasksByPosition, position, upgrade.getFirst(), true);
    if (nodeToNodeChange != 0) {
      // Second round gflag update tasks
      position = assertSequence(subTasksByPosition, MASTER, position, upgrade.getSecond());
      // Assert for ModifyBlackListTask before TSERVER rolling upgrade.
      if (upgrade.getSecond() == UpgradeOption.ROLLING_UPGRADE) {
        assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
      }
      position = assertSequence(subTasksByPosition, TSERVER, position, upgrade.getSecond());
    }

    assertEquals((int) expectedValues.getFirst(), position);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(expectedValues.getSecond())).nodeCommand(any(), any());

    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    if (EncryptionInTransitUtil.isRootCARequired(
        nodeToNode, clientToNode, rootAndClientRootCASame)) {
      assertEquals(taskParams.rootCA, universe.getUniverseDetails().rootCA);
    } else {
      assertNull(universe.getUniverseDetails().rootCA);
    }
    if (EncryptionInTransitUtil.isClientRootCARequired(
        nodeToNode, clientToNode, rootAndClientRootCASame)) {
      assertEquals(taskParams.getClientRootCA(), universe.getUniverseDetails().getClientRootCA());
    } else {
      assertNull(universe.getUniverseDetails().getClientRootCA());
    }
    assertEquals(
        nodeToNode,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt);
    assertEquals(
        clientToNode,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableClientToNodeEncrypt);
    assertEquals(rootAndClientRootCASame, universe.getUniverseDetails().rootAndClientRootCASame);
  }
}
