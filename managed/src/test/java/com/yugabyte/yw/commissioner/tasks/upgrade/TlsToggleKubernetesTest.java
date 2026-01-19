// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.*;
import static org.mockito.Mockito.*;

import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.lang3.RandomStringUtils;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.yaml.snakeyaml.Yaml;

public class TlsToggleKubernetesTest extends KubernetesUpgradeTaskTest {

  private TlsToggleKubernetes tlsToggleKubernetes;
  private UUID rootCAUUID;

  @Before
  public void setUp() {
    super.setUp();
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    this.tlsToggleKubernetes =
        new TlsToggleKubernetes(mockBaseTaskDependencies, mockOperatorStatusUpdaterFactory);
  }

  private TaskInfo submitTask(TlsToggleParams taskParams) {
    return submitTask(taskParams, TaskType.TlsToggleKubernetes, commissioner);
  }

  private void createRootCAUUID() throws Exception {
    createTempFile("tls_toggle_test_ca" + rootCAUUID + ".crt", "test data");
    createTempFile("tls_toggle_test_pk" + rootCAUUID + ".key", "test data");

    CertificateInfo.create(
        rootCAUUID,
        defaultCustomer.getUuid(),
        "test1" + RandomStringUtils.randomAlphanumeric(8),
        new Date(),
        new Date(),
        TestHelper.TMP_PATH + "/tls_toggle_test_pk" + rootCAUUID + ".key",
        TestHelper.TMP_PATH + "/tls_toggle_test_ca" + rootCAUUID + ".crt",
        CertConfigType.SelfSigned);
  }

  @Test
  public void testEnableTLS() throws Exception {
    tlsToggleKubernetes.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    rootCAUUID = UUID.randomUUID();
    createRootCAUUID();
    TlsToggleParams tlsToggleParams = new TlsToggleParams();
    tlsToggleParams.enableClientToNodeEncrypt = true;
    tlsToggleParams.enableNodeToNodeEncrypt = true;
    tlsToggleParams.rootAndClientRootCASame = true;
    tlsToggleParams.allowInsecure = false;
    tlsToggleParams.rootCA = rootCAUUID;
    tlsToggleParams.upgradeOption = SoftwareUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;
    TaskInfo taskInfo = submitTask(tlsToggleParams);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    verify(mockKubernetesManager, times(2))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);
    Map<String, Object> tlsInfo = (Map<String, Object>) overrides.get("tls");
    assertEquals(tlsInfo.get("enabled"), true);
    assertEquals(tlsInfo.get("nodeToNode"), true);
    assertEquals(tlsInfo.get("clientToServer"), true);
    assertEquals(tlsInfo.get("insecure"), false);

    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(TaskType.TlsToggleKubernetes, taskInfo.getTaskType());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(
        defaultUniverse
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .enableClientToNodeEncrypt);
    assertTrue(
        defaultUniverse
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .enableNodeToNodeEncrypt);
    assertTrue(defaultUniverse.getUniverseDetails().rootAndClientRootCASame);
    assertFalse(defaultUniverse.getUniverseDetails().allowInsecure);
  }

  @Test
  public void testDisableTLS() throws Exception {
    tlsToggleKubernetes.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    // Enable TLS
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.enableClientToNodeEncrypt = true;
    userIntent.enableNodeToNodeEncrypt = true;
    details.upsertPrimaryCluster(userIntent, null, null);
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    TlsToggleParams tlsToggleParams = new TlsToggleParams();
    tlsToggleParams.enableClientToNodeEncrypt = false;
    tlsToggleParams.enableNodeToNodeEncrypt = false;
    tlsToggleParams.rootAndClientRootCASame = true;
    tlsToggleParams.allowInsecure = false;
    tlsToggleParams.upgradeOption = SoftwareUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;
    TaskInfo taskInfo = submitTask(tlsToggleParams);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    verify(mockKubernetesManager, times(2))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture());

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    Yaml yaml = new Yaml();
    InputStream is = new FileInputStream(new File(expectedOverrideFile.getValue()));
    Map<String, Object> overrides = yaml.loadAs(is, Map.class);
    Map<String, Object> tlsInfo = (Map<String, Object>) overrides.get("tls");
    assertNull(tlsInfo);

    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(TaskType.TlsToggleKubernetes, taskInfo.getTaskType());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(
        defaultUniverse
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .enableClientToNodeEncrypt);
    assertFalse(
        defaultUniverse
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .enableNodeToNodeEncrypt);
    assertFalse(defaultUniverse.getUniverseDetails().allowInsecure);
  }
}
