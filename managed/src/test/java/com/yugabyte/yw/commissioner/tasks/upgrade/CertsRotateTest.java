// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.TlsConfigUpdateParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
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
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.MockitoAnnotations;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class CertsRotateTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks private CertsRotate certsRotate;

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_MASTER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.WaitForFollowerLag,
          TaskType.SetNodeState);

  private static final List<TaskType> ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.CheckUnderReplicatedTablets,
          TaskType.ModifyBlackList,
          TaskType.WaitForLeaderBlacklistCompletion,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForEncryptionKeyInMemory,
          TaskType.ModifyBlackList,
          TaskType.WaitForFollowerLag,
          TaskType.SetNodeState);

  private static final List<TaskType> NON_ROLLING_UPGRADE_TASK_SEQUENCE =
      ImmutableList.of(
          TaskType.SetNodeState,
          TaskType.AnsibleClusterServerCtl,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetNodeState);

  @Override
  @Before
  public void setUp() {
    super.setUp();
    MockitoAnnotations.initMocks(this);
    certsRotate.setUserTaskUUID(UUID.randomUUID());

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("underreplicated_tablets", Json.newArray());
    when(mockNodeUIApiHelper.getRequest(anyString())).thenReturn(bodyJson);
  }

  private TaskInfo submitTask(CertsRotateParams requestParams) {
    return submitTask(requestParams, TaskType.CertsRotate, commissioner);
  }

  private int assertSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      ServerType serverType,
      int startPosition,
      boolean isRollingUpgrade) {
    int position = startPosition;
    if (isRollingUpgrade) {
      List<TaskType> taskSequence =
          (serverType == MASTER
              ? ROLLING_UPGRADE_TASK_SEQUENCE_MASTER
              : ROLLING_UPGRADE_TASK_SEQUENCE_TSERVER);
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
          assertEquals(3, tasks.size());
          assertNodeSubTask(tasks, assertValues);
        }
        position++;
      }
    }
    return position;
  }

  private int assertCommonTasks(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      int position,
      boolean isRootCAUpdate,
      boolean isUniverseUpdate) {
    if (isRootCAUpdate || isUniverseUpdate) {
      if (isRootCAUpdate) {
        assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseUpdateRootCert);
      }
      if (isUniverseUpdate) {
        assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseSetTlsParams);
      }
    } else {
      List<TaskInfo> certUpdateTasks = subTasksByPosition.get(position++);
      assertTaskType(certUpdateTasks, TaskType.AnsibleConfigureServers);
      assertEquals(3, certUpdateTasks.size());
    }
    return position;
  }

  private int assertRestartSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition, int position, boolean isRollingUpgrade) {
    if (isRollingUpgrade) {
      position = assertSequence(subTasksByPosition, MASTER, position, true);
      assertTaskType(subTasksByPosition.get(position++), TaskType.ModifyBlackList);
      position = assertSequence(subTasksByPosition, TSERVER, position, true);
    } else {
      position = assertSequence(subTasksByPosition, MASTER, position, false);
      position = assertSequence(subTasksByPosition, TSERVER, position, false);
    }
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseConfig);
    return position;
  }

  private void assertUniverseDetails(
      CertsRotateParams taskParams,
      UUID rootCA,
      UUID clientRootCA,
      boolean currentNodeToNode,
      boolean currentClientToNode,
      boolean rootAndClientRootCASame,
      boolean rotateRootCA,
      boolean rotateClientRootCA,
      boolean isRootCARequired,
      boolean isClientRootCARequired) {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
    if (isRootCARequired) {
      if (rotateRootCA) {
        assertEquals(taskParams.rootCA, universeDetails.rootCA);
      } else {
        assertEquals(rootCA, universeDetails.rootCA);
      }
    } else {
      assertNull(universeDetails.rootCA);
    }
    if (isClientRootCARequired) {
      if (rotateClientRootCA) {
        assertEquals(taskParams.getClientRootCA(), universeDetails.getClientRootCA());
      } else {
        if (rootAndClientRootCASame) {
          assertEquals(universeDetails.rootCA, universeDetails.getClientRootCA());
        } else {
          assertEquals(taskParams.getClientRootCA(), universeDetails.getClientRootCA());
        }
      }
    } else {
      assertNull(universeDetails.getClientRootCA());
    }
    assertEquals(rootAndClientRootCASame, universeDetails.rootAndClientRootCASame);
    assertEquals(currentNodeToNode, userIntent.enableNodeToNodeEncrypt);
    assertEquals(currentClientToNode, userIntent.enableClientToNodeEncrypt);
  }

  private void prepareUniverse(
      boolean nodeToNode,
      boolean clientToNode,
      boolean rootAndClientRootCASame,
      UUID rootCA,
      UUID clientRootCA)
      throws IOException, NoSuchAlgorithmException {
    createTempFile("cert_rotate_test_ca.crt", "test data");

    CertificateInfo.create(
        rootCA,
        defaultCustomer.getUuid(),
        "test1",
        new Date(),
        new Date(),
        "privateKey",
        TestHelper.TMP_PATH + "/cert_rotate_test_ca.crt",
        CertConfigType.SelfSigned);

    if (!rootCA.equals(clientRootCA)) {
      CertificateInfo.create(
          clientRootCA,
          defaultCustomer.getUuid(),
          "test1",
          new Date(),
          new Date(),
          "privateKey",
          TestHelper.TMP_PATH + "/cert_rotate_test_ca.crt",
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

  private CertsRotateParams getTaskParams(
      boolean rotateRootCA,
      boolean rotateClientRootCA,
      boolean rootAndClientRootCASame,
      UpgradeOption upgradeOption)
      throws IOException, NoSuchAlgorithmException {
    CertsRotateParams taskParams = new CertsRotateParams();
    taskParams.upgradeOption = upgradeOption;
    taskParams.rootAndClientRootCASame = rootAndClientRootCASame;
    if (rotateRootCA) {
      taskParams.rootCA = UUID.randomUUID();
      CertificateInfo.create(
          taskParams.rootCA,
          defaultCustomer.getUuid(),
          "test1",
          new Date(),
          new Date(),
          "privateKey",
          TestHelper.TMP_PATH + "/cert_rotate_test_ca.crt",
          CertConfigType.SelfSigned);
    }
    if (rotateClientRootCA) {
      taskParams.setClientRootCA(UUID.randomUUID());
      CertificateInfo.create(
          taskParams.getClientRootCA(),
          defaultCustomer.getUuid(),
          "test1",
          new Date(),
          new Date(),
          "privateKey",
          TestHelper.TMP_PATH + "/cert_rotate_test_ca.crt",
          CertConfigType.SelfSigned);
    }
    if (rotateRootCA && rotateClientRootCA && rootAndClientRootCASame) {
      taskParams.setClientRootCA(taskParams.rootCA);
    }
    taskParams.rootAndClientRootCASame = rootAndClientRootCASame;

    return taskParams;
  }

  private CertsRotateParams getTaskParamsForSelfSignedServerCertRotation(
      boolean selfSignedServerCertRotate,
      boolean selfSignedClientCertRotate,
      boolean isRolling,
      boolean currentRootAndClientRootCASame) {
    CertsRotateParams taskParams = new CertsRotateParams();
    taskParams.upgradeOption =
        isRolling ? UpgradeOption.ROLLING_UPGRADE : UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.selfSignedServerCertRotate = selfSignedServerCertRotate;
    taskParams.selfSignedClientCertRotate = selfSignedClientCertRotate;
    taskParams.rootAndClientRootCASame = currentRootAndClientRootCASame;
    return taskParams;
  }

  public static Object[] getTestParameters() {
    return new Object[] {
      new Object[] {false, false, false, false, false, false},
      new Object[] {false, false, false, false, false, true},
      new Object[] {false, false, false, false, true, false},
      new Object[] {false, false, false, false, true, true},
      new Object[] {false, false, false, true, false, false},
      new Object[] {false, false, false, true, false, true},
      new Object[] {false, false, false, true, true, false},
      new Object[] {false, false, false, true, true, true},
      new Object[] {false, false, true, false, false, false},
      new Object[] {false, false, true, false, false, true},
      new Object[] {false, false, true, false, true, false},
      new Object[] {false, false, true, false, true, true},
      new Object[] {false, false, true, true, false, false},
      new Object[] {false, false, true, true, false, true},
      new Object[] {false, false, true, true, true, false},
      new Object[] {false, false, true, true, true, true},
      new Object[] {false, true, false, false, false, false},
      new Object[] {false, true, false, false, false, true},
      new Object[] {false, true, false, false, true, false},
      new Object[] {false, true, false, false, true, true},
      new Object[] {false, true, false, true, false, false},
      new Object[] {false, true, false, true, false, true},
      new Object[] {false, true, false, true, true, false},
      new Object[] {false, true, false, true, true, true},
      new Object[] {false, true, true, false, false, false},
      new Object[] {false, true, true, false, false, true},
      new Object[] {false, true, true, false, true, false},
      new Object[] {false, true, true, false, true, true},
      new Object[] {false, true, true, true, false, false},
      new Object[] {false, true, true, true, false, true},
      new Object[] {false, true, true, true, true, false},
      new Object[] {false, true, true, true, true, true},
      new Object[] {true, false, false, false, false, false},
      new Object[] {true, false, false, false, false, true},
      new Object[] {true, false, false, false, true, false},
      new Object[] {true, false, false, false, true, true},
      new Object[] {true, false, false, true, false, false},
      new Object[] {true, false, false, true, false, true},
      new Object[] {true, false, false, true, true, false},
      new Object[] {true, false, false, true, true, true},
      new Object[] {true, false, true, false, false, false},
      new Object[] {true, false, true, false, false, true},
      new Object[] {true, false, true, false, true, false},
      new Object[] {true, false, true, false, true, true},
      new Object[] {true, false, true, true, false, false},
      new Object[] {true, false, true, true, false, true},
      new Object[] {true, false, true, true, true, false},
      new Object[] {true, false, true, true, true, true},
      new Object[] {true, true, false, false, false, false},
      new Object[] {true, true, false, false, false, true},
      new Object[] {true, true, false, false, true, false},
      new Object[] {true, true, false, false, true, true},
      new Object[] {true, true, false, true, false, false},
      new Object[] {true, true, false, true, false, true},
      new Object[] {true, true, false, true, true, false},
      new Object[] {true, true, false, true, true, true},
      new Object[] {true, true, true, false, false, false},
      new Object[] {true, true, true, false, false, true},
      new Object[] {true, true, true, false, true, false},
      new Object[] {true, true, true, false, true, true},
      new Object[] {true, true, true, true, false, false},
      new Object[] {true, true, true, true, false, true},
      new Object[] {true, true, true, true, true, false},
      new Object[] {true, true, true, true, true, true}
    };
  }

  @Test
  public void testCertsRotateNonRestartUpgrade() throws IOException, NoSuchAlgorithmException {
    CertsRotateParams taskParams =
        getTaskParams(false, false, false, UpgradeOption.NON_RESTART_UPGRADE);
    TaskInfo taskInfo = submitTask(taskParams);
    if (taskInfo == null) {
      fail();
    }

    assertEquals(Failure, taskInfo.getTaskState());
    assertTrue(taskInfo.getSubTasks().isEmpty());
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
  }

  @Test
  @Parameters(method = "getTestParameters")
  public void testCertsRotateNonRollingUpgrade(
      boolean currentNodeToNode,
      boolean currentClientToNode,
      boolean currentRootAndClientRootCASame,
      boolean rotateRootCA,
      boolean rotateClientRootCA,
      boolean rootAndClientRootCASame)
      throws IOException, NoSuchAlgorithmException {

    if (rootAndClientRootCASame && rotateClientRootCA) {
      // if clientRootCA is rotated, bothCASame flag cannot be true
      rootAndClientRootCASame = false;
    }

    UUID rootCA = UUID.randomUUID();
    UUID clientRootCA = currentRootAndClientRootCASame ? rootCA : UUID.randomUUID();
    prepareUniverse(
        currentNodeToNode,
        currentClientToNode,
        currentRootAndClientRootCASame,
        rootCA,
        clientRootCA);
    CertsRotateParams taskParams =
        getTaskParams(
            rotateRootCA,
            rotateClientRootCA,
            rootAndClientRootCASame,
            UpgradeOption.NON_ROLLING_UPGRADE);

    TaskInfo taskInfo = submitTask(taskParams);
    if (taskInfo == null) {
      fail();
    }

    boolean isRootCARequired =
        EncryptionInTransitUtil.isRootCARequired(
            currentNodeToNode, currentClientToNode, rootAndClientRootCASame);
    boolean isClientRootCARequired =
        EncryptionInTransitUtil.isClientRootCARequired(
            currentNodeToNode, currentClientToNode, rootAndClientRootCASame);

    // Expected failure scenarios
    if ((!isRootCARequired && rotateRootCA)
        || (!isClientRootCARequired && rotateClientRootCA)
        // clientRootCA is always required for hot-cert-reload,
        // so below condition is no more valid
        //         || (isClientRootCARequired && !rotateClientRootCA &&
        // currentRootAndClientRootCASame)
        || (!rotateRootCA && !rotateClientRootCA)
        || (rootAndClientRootCASame && !currentClientToNode && !rotateClientRootCA)) {
      if (!(!rotateRootCA
          && !rotateClientRootCA
          && currentNodeToNode
          && currentClientToNode
          && !currentRootAndClientRootCASame
          && rootAndClientRootCASame)) {
        assertEquals(Failure, taskInfo.getTaskState());
        assertTrue(taskInfo.getSubTasks().isEmpty());
        verify(mockNodeManager, times(0)).nodeCommand(any(), any());
        return;
      }
    }

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    // RootCA update task
    int expectedPosition = 15;
    if (rotateRootCA) {
      expectedPosition += 2;
      position = assertCommonTasks(subTasksByPosition, position, true, false);
    }
    // Cert update tasks
    position = assertCommonTasks(subTasksByPosition, position, false, false);
    // gflags update tasks
    position = assertCommonTasks(subTasksByPosition, position, false, false);
    position = assertCommonTasks(subTasksByPosition, position, false, false);
    // Restart tasks
    position = assertRestartSequence(subTasksByPosition, position, false);
    // RootCA update task
    if (rotateRootCA) {
      position = assertCommonTasks(subTasksByPosition, position, true, false);
    }
    // Update universe params task
    position = assertCommonTasks(subTasksByPosition, position, false, true);

    assertEquals(expectedPosition, position);
    verify(mockNodeManager, times(21)).nodeCommand(any(), any());

    assertUniverseDetails(
        taskParams,
        rootCA,
        clientRootCA,
        currentNodeToNode,
        currentClientToNode,
        rootAndClientRootCASame,
        rotateRootCA,
        rotateClientRootCA,
        isRootCARequired,
        isClientRootCARequired);
  }

  @Test
  @Parameters(method = "getTestParameters")
  public void testCertsRotateRollingUpgrade(
      boolean currentNodeToNode,
      boolean currentClientToNode,
      boolean currentRootAndClientRootCASame,
      boolean rotateRootCA,
      boolean rotateClientRootCA,
      boolean rootAndClientRootCASame)
      throws IOException, NoSuchAlgorithmException {
    UUID rootCA = UUID.randomUUID();
    UUID clientRootCA = UUID.randomUUID();

    if (rootAndClientRootCASame && rotateClientRootCA) {
      // if clientRootCA is rotated, bothCASame flag cannot be true
      rootAndClientRootCASame = false;
    }

    prepareUniverse(
        currentNodeToNode,
        currentClientToNode,
        currentRootAndClientRootCASame,
        rootCA,
        clientRootCA);
    CertsRotateParams taskParams =
        getTaskParams(
            rotateRootCA,
            rotateClientRootCA,
            rootAndClientRootCASame,
            UpgradeOption.ROLLING_UPGRADE);

    TaskInfo taskInfo = submitTask(taskParams);
    if (taskInfo == null) {
      fail();
    }

    boolean isRootCARequired =
        EncryptionInTransitUtil.isRootCARequired(
            currentNodeToNode, currentClientToNode, rootAndClientRootCASame);
    boolean isClientRootCARequired =
        EncryptionInTransitUtil.isClientRootCARequired(
            currentNodeToNode, currentClientToNode, rootAndClientRootCASame);

    // Expected failure scenarios
    if ((!isRootCARequired && rotateRootCA)
        || (!isClientRootCARequired && rotateClientRootCA)
        //        || (isClientRootCARequired && !rotateClientRootCA &&
        // currentRootAndClientRootCASame)
        //           && !rootAndClientRootCASame && rotateRootCA)
        || (!rotateRootCA && !rotateClientRootCA)
        // if bothCA are same, but client encryption not turned on, it would fail
        || (rootAndClientRootCASame && !currentClientToNode && !rotateClientRootCA)) {
      if (!(!rotateRootCA
          && !rotateClientRootCA
          && currentNodeToNode
          && currentClientToNode
          && !currentRootAndClientRootCASame
          && rootAndClientRootCASame)) {
        assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
        assertTrue(taskInfo.getSubTasks().isEmpty());
        verify(mockNodeManager, times(0)).nodeCommand(any(), any());
        return;
      }
    }

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    int expectedPosition = 66;
    int expectedNumberOfInvocations = 21;
    if (rotateRootCA) {
      expectedPosition += 128;
      expectedNumberOfInvocations += 30;
      // RootCA update task
      position = assertCommonTasks(subTasksByPosition, position, true, false);
      // Cert update tasks
      position = assertCommonTasks(subTasksByPosition, position, false, false);
      // Restart tasks
      position = assertRestartSequence(subTasksByPosition, position, true);
      // Second round cert update tasks
      position = assertCommonTasks(subTasksByPosition, position, false, false);
      // Second round restart tasks
      position = assertRestartSequence(subTasksByPosition, position, true);
      // Third round cert update tasks
      position = assertCommonTasks(subTasksByPosition, position, false, false);
      // gflags update tasks
      position = assertCommonTasks(subTasksByPosition, position, false, false);
      position = assertCommonTasks(subTasksByPosition, position, false, false);
      // Update universe params task
      position = assertCommonTasks(subTasksByPosition, position, true, true);
      // Third round restart tasks
      position = assertRestartSequence(subTasksByPosition, position, true);
    } else {
      // Cert update tasks
      position = assertCommonTasks(subTasksByPosition, position, false, false);
      // gflags update tasks
      position = assertCommonTasks(subTasksByPosition, position, false, false);
      position = assertCommonTasks(subTasksByPosition, position, false, false);
      // Restart tasks
      position = assertRestartSequence(subTasksByPosition, position, true);
      // Update universe params task
      position = assertCommonTasks(subTasksByPosition, position, false, true);
    }

    assertEquals(expectedPosition, position);
    verify(mockNodeManager, times(expectedNumberOfInvocations)).nodeCommand(any(), any());

    assertUniverseDetails(
        taskParams,
        rootCA,
        clientRootCA,
        currentNodeToNode,
        currentClientToNode,
        rootAndClientRootCASame,
        rotateRootCA,
        rotateClientRootCA,
        isRootCARequired,
        isClientRootCARequired);
  }

  @Test
  @Parameters(method = "getTestParameters")
  public void testCertsRotateSelfSignedServerCert(
      boolean currentNodeToNode,
      boolean currentClientToNode,
      boolean currentRootAndClientRootCASame,
      boolean selfSignedServerCertRotate,
      boolean selfSignedClientCertRotate,
      boolean isRolling)
      throws IOException, NoSuchAlgorithmException {
    UUID rootCA = UUID.randomUUID();
    UUID clientRootCA = UUID.randomUUID();

    if (!currentNodeToNode || !currentClientToNode) {
      // rootCA and clientRootCA cannot be same
      currentRootAndClientRootCASame = false;
    }

    prepareUniverse(
        currentNodeToNode,
        currentClientToNode,
        currentRootAndClientRootCASame,
        rootCA,
        clientRootCA);
    CertsRotateParams taskParams =
        getTaskParamsForSelfSignedServerCertRotation(
            selfSignedServerCertRotate,
            selfSignedClientCertRotate,
            isRolling,
            currentRootAndClientRootCASame);

    TaskInfo taskInfo = submitTask(taskParams);
    if (taskInfo == null) {
      fail();
    }

    boolean isRootCARequired =
        EncryptionInTransitUtil.isRootCARequired(
            currentNodeToNode, currentClientToNode, currentRootAndClientRootCASame);
    boolean isClientRootCARequired =
        EncryptionInTransitUtil.isClientRootCARequired(
            currentNodeToNode, currentClientToNode, currentRootAndClientRootCASame);

    // Expected failure scenarios
    if (!((isRootCARequired && selfSignedServerCertRotate)
        || (isClientRootCARequired && selfSignedClientCertRotate))) {
      assertEquals(Failure, taskInfo.getTaskState());
      assertTrue(taskInfo.getSubTasks().isEmpty());
      verify(mockNodeManager, times(0)).nodeCommand(any(), any());
      return;
    }

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    int position = 0;
    // RootCA update task
    int expectedPosition = isRolling ? 66 : 15;
    // Cert update tasks
    position = assertCommonTasks(subTasksByPosition, position, false, false);
    // gflags update tasks
    position = assertCommonTasks(subTasksByPosition, position, false, false);
    position = assertCommonTasks(subTasksByPosition, position, false, false);
    // Restart tasks
    position = assertRestartSequence(subTasksByPosition, position, isRolling);
    // Update universe params task
    position = assertCommonTasks(subTasksByPosition, position, false, true);

    assertEquals(expectedPosition, position);
    verify(mockNodeManager, times(21)).nodeCommand(any(), any());

    assertUniverseDetails(
        taskParams,
        rootCA,
        clientRootCA,
        currentNodeToNode,
        currentClientToNode,
        currentRootAndClientRootCASame,
        false,
        false,
        isRootCARequired,
        isClientRootCARequired);
  }

  @Test
  public void testCertsRotateMerge() {

    // prepare dummy univ details
    UUID rootCAUUID = UUID.randomUUID();
    UUID clientCAUUID = UUID.randomUUID();
    UniverseDefinitionTaskParams univParams = new UniverseDefinitionTaskParams();
    univParams.allowInsecure = false;
    univParams.nodePrefix = "foo";
    univParams.setTxnTableWaitCountFlag = true;
    univParams.rootAndClientRootCASame = false;
    univParams.rootCA = rootCAUUID;
    univParams.setClientRootCA(null);
    univParams.expectedUniverseVersion = -1;

    univParams.clusters = new ArrayList<>();
    UserIntent userIntent = new UserIntent();
    userIntent.replicationFactor = 1;
    userIntent.numNodes = 4;
    userIntent.assignPublicIP = false;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.masterGFlags.put("flag", "value");
    userIntent.tserverGFlags.put("flag", "value");
    univParams.clusters.add(new Cluster(ClusterType.PRIMARY, userIntent));

    // prepare dummy tls update params
    TlsConfigUpdateParams tlsUpdateParams = new TlsConfigUpdateParams();
    tlsUpdateParams.allowInsecure = true;
    tlsUpdateParams.setClientRootCA(clientCAUUID);
    tlsUpdateParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    tlsUpdateParams.setTxnTableWaitCountFlag = false;
    tlsUpdateParams.selfSignedClientCertRotate = true;
    tlsUpdateParams.rootAndClientRootCASame = true;
    tlsUpdateParams.selfSignedServerCertRotate = false;
    tlsUpdateParams.rootCA = null;

    // verify merged params
    CertsRotateParams certsParams =
        CertsRotateParams.mergeUniverseDetails(tlsUpdateParams, univParams);
    assertNotNull(certsParams);
    UniverseDefinitionTaskParams certsUnivParams = (UniverseDefinitionTaskParams) certsParams;
    assertNotNull(certsUnivParams);
    assertNotNull(univParams);

    // Compare just the UniverseDefinitionTaskParams part of certsRotateParams and univParams
    // The only parts that we expect to change are the tls related fields
    univParams.rootAndClientRootCASame = tlsUpdateParams.rootAndClientRootCASame;
    univParams.setClientRootCA(tlsUpdateParams.getClientRootCA());
    univParams.rootCA = tlsUpdateParams.rootCA;
    ObjectMapper mapper = new ObjectMapper();
    try {
      String certsUnivString =
          mapper.writerFor(UniverseDefinitionTaskParams.class).writeValueAsString(certsUnivParams);
      String univString =
          mapper.writerFor(UniverseDefinitionTaskParams.class).writeValueAsString(univParams);
      log.info("certs params univ definition is {}.", certsUnivString);
      log.info("original univ definition is {}.", univString);
      assertEquals(certsUnivString, univString);
    } catch (JsonProcessingException jpex) {
      throw new RuntimeException("unable to serialize to json.");
    }

    // verify that certs-specific fields in certs rotate params match the tls update params
    assertEquals(certsParams.rootCA, tlsUpdateParams.rootCA);
    assertEquals(certsParams.getClientRootCA(), tlsUpdateParams.getClientRootCA());
    assertEquals(certsParams.rootAndClientRootCASame, tlsUpdateParams.rootAndClientRootCASame);
    assertEquals(certsParams.upgradeOption, tlsUpdateParams.upgradeOption);
    assertEquals(
        certsParams.selfSignedClientCertRotate, tlsUpdateParams.selfSignedClientCertRotate);
    assertEquals(
        certsParams.selfSignedServerCertRotate, tlsUpdateParams.selfSignedServerCertRotate);
  }
}
