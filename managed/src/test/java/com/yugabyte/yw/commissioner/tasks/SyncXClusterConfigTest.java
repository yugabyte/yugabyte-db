// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.cdc.CdcConsumer.ConsumerRegistryPB;
import org.yb.cdc.CdcConsumer.ProducerEntryPB;
import org.yb.cdc.CdcConsumer.StreamEntryPB;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;
import play.libs.F.Tuple;

@RunWith(MockitoJUnitRunner.class)
public class SyncXClusterConfigTest extends CommissionerBaseTest {

  private String configName;
  private String sourceUniverseName;
  private UUID sourceUniverseUUID;
  private Users defaultUser;
  private Universe sourceUniverse;
  private String targetUniverseName;
  private UUID targetUniverseUUID;
  private Universe targetUniverse;
  private String exampleTableID1;
  private String exampleTableID2;
  private String exampleTableID3;
  private Set<String> exampleTables;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;

  @Before
  @Override
  public void setUp() {
    super.setUp();

    defaultCustomer = testCustomer("SyncXClusterConfig-test-customer");
    defaultUser = ModelFactory.testUser(defaultCustomer);
    configName = "SyncXClusterConfigTest-test-config";

    sourceUniverseName = "SyncXClusterConfig-test-universe-1";
    sourceUniverseUUID = UUID.randomUUID();
    sourceUniverse = createUniverse(sourceUniverseName, sourceUniverseUUID);

    targetUniverseName = "SyncXClusterConfig-test-universe-2";
    targetUniverseUUID = UUID.randomUUID();
    targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);

    exampleTableID1 = "000030af000030008000000000004000";
    exampleTableID2 = "000030af000030008000000000004001";
    exampleTableID3 = "000030af000030008000000000004002";

    exampleTables = new HashSet<>();
    exampleTables.add(exampleTableID1);
    exampleTables.add(exampleTableID2);

    createFormData = new XClusterConfigCreateFormData();
    createFormData.name = configName;
    createFormData.sourceUniverseUUID = sourceUniverseUUID;
    createFormData.targetUniverseUUID = targetUniverseUUID;
    createFormData.tables = exampleTables;

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(targetUniverseMasterAddresses, targetUniverseCertificate))
        .thenReturn(mockClient);
  }

  private TaskInfo submitTask(Universe targetUniverse) {
    XClusterConfigTaskParams taskParams = new XClusterConfigTaskParams(targetUniverseUUID);
    try {
      UUID taskUUID = commissioner.submit(TaskType.SyncXClusterConfig, taskParams);
      // Set http context
      TestUtils.setFakeHttpContext(defaultUser);
      CustomerTask.create(
          defaultCustomer,
          targetUniverse.getUniverseUUID(),
          taskUUID,
          TargetType.Universe,
          CustomerTask.TaskType.SyncXClusterConfig,
          targetUniverse.getName());
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private void setupMockClusterConfig(
      List<Tuple<XClusterConfigCreateFormData, Boolean>> xClusterConfigInfoList,
      MasterTypes.MasterErrorPB getConfigError) {
    ConsumerRegistryPB.Builder fakeConsumerRegistryBuilder = ConsumerRegistryPB.newBuilder();

    for (Tuple<XClusterConfigCreateFormData, Boolean> xClusterConfigInfo : xClusterConfigInfoList) {
      XClusterConfigCreateFormData xClusterConfig = xClusterConfigInfo._1;
      Boolean isPaused = xClusterConfigInfo._2;

      ProducerEntryPB.Builder fakeProducerEntry =
          ProducerEntryPB.newBuilder().setDisableStream(isPaused);

      for (String tableID : xClusterConfig.tables) {
        StreamEntryPB.Builder fakeStreamEntry =
            StreamEntryPB.newBuilder().setProducerTableId(tableID);

        // Note: Using random uuid for stream ID. This is just to generate fake data. Actual
        // stream IDs are not UUIDs.
        fakeProducerEntry.putStreamMap(UUID.randomUUID().toString(), fakeStreamEntry.build());
      }

      String replicationGroupName = xClusterConfig.sourceUniverseUUID + "_" + xClusterConfig.name;
      fakeConsumerRegistryBuilder.putProducerMap(replicationGroupName, fakeProducerEntry.build());
    }

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder fakeClusterConfigBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder()
            .setConsumerRegistry(fakeConsumerRegistryBuilder.build());

    GetMasterClusterConfigResponse fakeClusterConfigResponse =
        new GetMasterClusterConfigResponse(0, "", fakeClusterConfigBuilder.build(), getConfigError);

    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
    } catch (Exception e) {
    }
  }

  @Test
  public void testSyncImportOne() {
    List<Tuple<XClusterConfigCreateFormData, Boolean>> xClusterConfigInfoList =
        Collections.singletonList(new Tuple<>(createFormData, false));
    setupMockClusterConfig(xClusterConfigInfoList, null);

    TaskInfo taskInfo = submitTask(targetUniverse);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(1, configList.size());

    XClusterConfig actual = configList.get(0);
    assertEquals(createFormData.name, actual.getName());
    assertEquals(createFormData.sourceUniverseUUID, actual.getSourceUniverseUUID());
    assertEquals(createFormData.targetUniverseUUID, actual.getTargetUniverseUUID());
    assertEquals(createFormData.tables, actual.getTableIds());
    assertEquals(XClusterConfigStatusType.Running, actual.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    actual.delete();
  }

  @Test
  public void testSyncImportOneHAEnabled() {
    List<Tuple<XClusterConfigCreateFormData, Boolean>> xClusterConfigInfoList =
        Collections.singletonList(new Tuple<>(createFormData, false));
    setupMockClusterConfig(xClusterConfigInfoList, null);

    HighAvailabilityConfig.create("test-cluster-key");

    TaskInfo taskInfo = submitTask(targetUniverse);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(1, configList.size());

    XClusterConfig actual = configList.get(0);
    assertEquals(createFormData.name, actual.getName());
    assertEquals(createFormData.sourceUniverseUUID, actual.getSourceUniverseUUID());
    assertEquals(createFormData.targetUniverseUUID, actual.getTargetUniverseUUID());
    assertEquals(createFormData.tables, actual.getTableIds());
    assertEquals(XClusterConfigStatusType.Running, actual.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    actual.delete();
  }

  @Test
  public void testSyncImportMultiple() {
    XClusterConfigCreateFormData expectedConfig1 = new XClusterConfigCreateFormData();
    expectedConfig1.name = "config-1";
    expectedConfig1.sourceUniverseUUID = sourceUniverseUUID;
    expectedConfig1.targetUniverseUUID = targetUniverseUUID;
    expectedConfig1.tables = Collections.singleton(exampleTableID1);
    XClusterConfigCreateFormData expectedConfig2 = new XClusterConfigCreateFormData();
    expectedConfig2.name = "config-2";
    expectedConfig2.sourceUniverseUUID = sourceUniverseUUID;
    expectedConfig2.targetUniverseUUID = targetUniverseUUID;
    expectedConfig2.tables = Collections.singleton(exampleTableID2);
    XClusterConfigCreateFormData expectedConfig3 = new XClusterConfigCreateFormData();
    expectedConfig3.name = "config-3";
    expectedConfig3.sourceUniverseUUID = sourceUniverseUUID;
    expectedConfig3.targetUniverseUUID = targetUniverseUUID;
    expectedConfig3.tables = Collections.singleton(exampleTableID3);
    List<Tuple<XClusterConfigCreateFormData, Boolean>> xClusterConfigInfoList = new ArrayList<>();
    xClusterConfigInfoList.add(new Tuple<>(expectedConfig1, false));
    xClusterConfigInfoList.add(new Tuple<>(expectedConfig2, true));
    xClusterConfigInfoList.add(new Tuple<>(expectedConfig3, false));
    setupMockClusterConfig(xClusterConfigInfoList, null);

    TaskInfo taskInfo = submitTask(targetUniverse);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(xClusterConfigInfoList.size(), configList.size());

    for (Tuple<XClusterConfigCreateFormData, Boolean> xClusterConfigInfo : xClusterConfigInfoList) {
      XClusterConfigCreateFormData expectedXClusterConfig = xClusterConfigInfo._1;
      Boolean expectedIsPaused = xClusterConfigInfo._2;

      XClusterConfig actual =
          XClusterConfig.getByNameSourceTarget(
              expectedXClusterConfig.name,
              expectedXClusterConfig.sourceUniverseUUID,
              expectedXClusterConfig.targetUniverseUUID);

      assertEquals(expectedXClusterConfig.name, actual.getName());
      assertEquals(expectedXClusterConfig.sourceUniverseUUID, actual.getSourceUniverseUUID());
      assertEquals(expectedXClusterConfig.targetUniverseUUID, actual.getTargetUniverseUUID());
      assertEquals(expectedXClusterConfig.tables, actual.getTableIds());
      if (expectedIsPaused) {
        assertEquals(XClusterConfigStatusType.Running, actual.getStatus());
        assertTrue(actual.isPaused());
      } else {
        assertEquals(XClusterConfigStatusType.Running, actual.getStatus());
      }
    }

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    for (XClusterConfig xClusterConfig : configList) {
      xClusterConfig.delete();
    }
  }

  @Test
  public void testSyncModifyExistingTables() {
    XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    Set<String> expectedTables = new HashSet<>();
    expectedTables.add(exampleTableID1);
    expectedTables.add(exampleTableID2);
    expectedTables.add(exampleTableID3);
    createFormData.tables = expectedTables;
    List<Tuple<XClusterConfigCreateFormData, Boolean>> xClusterConfigInfoList =
        Collections.singletonList(new Tuple<>(createFormData, true));
    setupMockClusterConfig(xClusterConfigInfoList, null);

    TaskInfo taskInfo = submitTask(targetUniverse);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(1, configList.size());

    XClusterConfig actual = configList.get(0);
    assertEquals(createFormData.name, actual.getName());
    assertEquals(createFormData.sourceUniverseUUID, actual.getSourceUniverseUUID());
    assertEquals(createFormData.targetUniverseUUID, actual.getTargetUniverseUUID());
    assertEquals(expectedTables, actual.getTableIds());
    assertEquals(XClusterConfigStatusType.Running, actual.getStatus());
    assertTrue(actual.isPaused());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    actual.delete();
  }

  @Test
  public void testSyncModifyExistingStatus() {
    XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    List<Tuple<XClusterConfigCreateFormData, Boolean>> xClusterConfigInfoList =
        Collections.singletonList(new Tuple<>(createFormData, true));
    setupMockClusterConfig(xClusterConfigInfoList, null);

    TaskInfo taskInfo = submitTask(targetUniverse);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(1, configList.size());

    XClusterConfig actual = configList.get(0);
    assertEquals(createFormData.name, actual.getName());
    assertEquals(createFormData.sourceUniverseUUID, actual.getSourceUniverseUUID());
    assertEquals(createFormData.targetUniverseUUID, actual.getTargetUniverseUUID());
    assertEquals(createFormData.tables, actual.getTableIds());
    assertEquals(XClusterConfigStatusType.Running, actual.getStatus());
    assertTrue(actual.isPaused());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    actual.delete();
  }

  @Test
  public void testSyncFixesFailed() {
    XClusterConfig.create(createFormData, XClusterConfigStatusType.Failed);

    List<Tuple<XClusterConfigCreateFormData, Boolean>> xClusterConfigInfoList =
        Collections.singletonList(new Tuple<>(createFormData, false));
    setupMockClusterConfig(xClusterConfigInfoList, null);

    TaskInfo taskInfo = submitTask(targetUniverse);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(1, configList.size());

    XClusterConfig actual = configList.get(0);
    assertEquals(createFormData.name, actual.getName());
    assertEquals(createFormData.sourceUniverseUUID, actual.getSourceUniverseUUID());
    assertEquals(createFormData.targetUniverseUUID, actual.getTargetUniverseUUID());
    assertEquals(createFormData.tables, actual.getTableIds());
    assertEquals(XClusterConfigStatusType.Running, actual.getStatus());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);

    actual.delete();
  }

  @Test
  public void testSyncDeleteExisting() {
    XClusterConfig.create(createFormData, XClusterConfigStatusType.Failed);

    setupMockClusterConfig(Collections.emptyList(), null);

    TaskInfo taskInfo = submitTask(targetUniverse);
    assertNotNull(taskInfo);
    assertEquals(Success, taskInfo.getTaskState());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(0, configList.size());

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertTrue("update successful", targetUniverse.getUniverseDetails().updateSucceeded);
  }

  @Test
  public void testSyncError() {
    List<Tuple<XClusterConfigCreateFormData, Boolean>> xClusterConfigInfoList =
        Collections.singletonList(new Tuple<>(createFormData, false));
    String syncErrMsg = "failed to run sync rpc";
    AppStatusPB.Builder appStatusBuilder =
        AppStatusPB.newBuilder().setMessage(syncErrMsg).setCode(ErrorCode.UNKNOWN_ERROR);
    MasterErrorPB.Builder masterErrorBuilder =
        MasterErrorPB.newBuilder().setStatus(appStatusBuilder.build()).setCode(Code.UNKNOWN_ERROR);
    setupMockClusterConfig(xClusterConfigInfoList, masterErrorBuilder.build());

    TaskInfo taskInfo = submitTask(targetUniverse);
    assertNotNull(taskInfo);
    assertEquals(Failure, taskInfo.getTaskState());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(0, configList.size());

    assertEquals(TaskType.XClusterConfigSync, taskInfo.getSubTasks().get(1).getTaskType());
    String taskErrMsg = taskInfo.getSubTasks().get(1).getErrorMessage();
    String expectedErrMsg =
        String.format(
            "Failed to getMasterClusterConfig from target universe (%s): %s",
            targetUniverseUUID, syncErrMsg);
    assertThat(taskErrMsg, containsString(expectedErrMsg));

    targetUniverse = Universe.getOrBadRequest(targetUniverseUUID);
    assertEquals(1, targetUniverse.getVersion());
    assertFalse("universe unlocked", targetUniverse.universeIsLocked());
    assertFalse("update completed", targetUniverse.getUniverseDetails().updateInProgress);
    assertFalse("update failed", targetUniverse.getUniverseDetails().updateSucceeded);
  }
}
