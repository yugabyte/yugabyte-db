// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.UpgradeYsqlResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterClusterOuterClass.PromoteAutoFlagsResponsePB;

public class SoftwareKubernetesUpgradeYBTest extends KubernetesUpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private SoftwareKubernetesUpgrade softwareKubernetesUpgrade;

  @Before
  public void setUp() {
    super.setUp();
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    try {
      when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
          .thenReturn(
              new PromoteAutoFlagsResponse(
                  0, "uuid", PromoteAutoFlagsResponsePB.getDefaultInstance()));
      when(mockGFlagsValidation.ysqlMajorVersionUpgrade(anyString(), anyString()))
          .thenReturn(false);
    } catch (Exception ignored) {
      fail();
    }
    this.softwareKubernetesUpgrade =
        new SoftwareKubernetesUpgrade(
            mockBaseTaskDependencies, null, mockOperatorStatusUpdaterFactory);
  }

  private TaskInfo submitTask(SoftwareUpgradeParams taskParams) {
    return submitTask(taskParams, TaskType.SoftwareKubernetesUpgradeYB, commissioner);
  }

  @Test
  public void testSoftwareUpgradeSingleAZ() throws IOException {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    TaskInfo taskInfo = submitTask(taskParams);

    verify(mockKubernetesManager, times(9))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(9))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(2))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        YB_SOFTWARE_VERSION_OLD,
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testSoftwareUpgradeWithAutoFinalize() throws IOException {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");

    UpgradeYsqlResponse mockUpgradeYsqlResponse = new UpgradeYsqlResponse(1000, "", null);
    IsInitDbDoneResponse mockIsInitDbDoneResponse =
        new IsInitDbDoneResponse(1000, "", true, true, null, null);
    mockClient = mock(YBClient.class);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);

    try {
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockUpgradeYsqlResponse);
      when(mockClient.getIsInitDbDone()).thenReturn(mockIsInitDbDoneResponse);
    } catch (Exception ignored) {
      fail();
    }

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    taskParams.rollbackSupport = false;
    TaskInfo taskInfo = submitTask(taskParams);

    verify(mockKubernetesManager, times(9))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(9))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(2))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testSoftwareUpgradeMultiAZ() throws IOException {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseMultiAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(false);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    TaskInfo taskInfo = submitTask(taskParams);

    verify(mockKubernetesManager, times(9))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());
    verify(mockKubernetesManager, times(9))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(6))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertTrue(expectedNodePrefix.getValue().contains(NODE_PREFIX));
    assertTrue(expectedNamespace.getValue().contains(NODE_PREFIX));
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        YB_SOFTWARE_VERSION_OLD,
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testSoftwareKubernetesUpgradeRetries() {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;

    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.SoftwareUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.SoftwareKubernetesUpgradeYB,
        taskParams);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }
}
