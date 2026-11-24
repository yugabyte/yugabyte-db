// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.RpcClientCert.CertAndKey;
import com.yugabyte.yw.common.certmgmt.RpcClientCertTest;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.EnableNodeToNodeClientCertsParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.yaml.snakeyaml.Yaml;

public class EnableNodeToNodeClientCertsTest extends KubernetesUpgradeTaskTest {

  private static final String SUPPORTED_VERSION = "2.31.0.0-b700";

  private UUID rootCA;

  @Before
  public void setUp() {
    super.setUp();
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
  }

  // A K8s universe that supports client certs but doesn't use them yet.
  private void setupSupportedUniverse() throws Exception {
    setupUniverseSingleAZ(false /* setMasters */, true /* mockGetLeaderMaster */);
    String caCertFile =
        TestHelper.createTempFile(
            TestHelper.TMP_PATH + "/" + UUID.randomUUID(),
            "ca.root.crt",
            CertificateHelper.getAsPemString(RpcClientCertTest.ROOT_CA.getRight()));
    rootCA = UUID.randomUUID();
    CertificateInfo.create(
        rootCA,
        defaultCustomer.getUuid(),
        "cert-manager-ca",
        new Date(),
        new Date(),
        "",
        caCertFile,
        CertConfigType.K8SCertManager);
    updateUniverse(
        details -> {
          details.rootCA = rootCA;
          UserIntent intent = details.getPrimaryCluster().userIntent;
          intent.enableNodeToNodeEncrypt = true;
          intent.ybSoftwareVersion = SUPPORTED_VERSION;
        });
  }

  // Saved without a version bump, as submitTask expects version 2.
  private void updateUniverse(Consumer<UniverseDefinitionTaskParams> change) {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              change.accept(details);
              u.setUniverseDetails(details);
            },
            false);
  }

  private void assertRejected(EnableNodeToNodeClientCertsParams params, String expectedMessage) {
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () ->
                params.verifyParams(
                    Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()), true));
    assertTrue(e.getMessage(), e.getMessage().contains(expectedMessage));
  }

  @Test
  public void testEnable() throws Exception {
    setupSupportedUniverse();
    CertAndKey pem = RpcClientCertTest.newCertPem(Duration.ofDays(90));
    when(mockKubernetesManager.getSecret(any(), any(), any()))
        .thenReturn(RpcClientCertTest.secret(pem));

    // The controller binds the universe's details; submitTask copies only some of them.
    EnableNodeToNodeClientCertsParams params = new EnableNodeToNodeClientCertsParams();
    params.rootCA = rootCA;
    TaskInfo taskInfo = submitTask(params, TaskType.EnableNodeToNodeClientCerts, commissioner);
    assertEquals(
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskState() != Success)
            .map(t -> t.getTaskType() + ": " + t.getErrorMessage())
            .collect(Collectors.joining("; ")),
        Success,
        taskInfo.getTaskState());

    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useNodeToNodeClientCerts);
    assertEquals(
        pem.cert(), Files.readString(Paths.get(CertificateHelper.getClientCertFile(rootCA))));

    // The cert is loaded, then the setting saved, before any pod restarts.
    List<TaskType> types =
        taskInfo.getSubTasks().stream()
            .sorted(Comparator.comparingInt(TaskInfo::getPosition))
            .map(TaskInfo::getTaskType)
            .distinct()
            .collect(Collectors.toList());
    int wait = types.indexOf(TaskType.WaitForRpcClientCert);
    int save = types.indexOf(TaskType.UpdateUniverseFields);
    int firstWaitForPod = types.indexOf(TaskType.KubernetesWaitForPod);
    assertTrue(types.toString(), wait >= 0 && wait < save && save < firstWaitForPod);

    ArgumentCaptor<String> overrideFile = ArgumentCaptor.forClass(String.class);
    verify(mockKubernetesManager, atLeastOnce())
        .helmUpgrade(any(), any(), any(), any(), any(), overrideFile.capture(), isNull());
    try (InputStream is = new FileInputStream(overrideFile.getValue())) {
      Map<String, Object> overrides = new Yaml().loadAs(is, Map.class);
      Map<String, Map<String, Object>> gflags =
          (Map<String, Map<String, Object>>) overrides.get("gflags");
      for (String server : List.of("master", "tserver")) {
        assertEquals("true", gflags.get(server).get(GFlagsUtil.USE_CLIENT_CERTIFICATES));
      }
    }
  }

  @Test
  public void testRejectsRolling() throws Exception {
    setupSupportedUniverse();
    EnableNodeToNodeClientCertsParams params = new EnableNodeToNodeClientCertsParams();
    params.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
    assertRejected(params, "non-rolling");
  }

  @Test
  public void testRejectsAlreadyEnabled() throws Exception {
    setupSupportedUniverse();
    updateUniverse(d -> d.getPrimaryCluster().userIntent.useNodeToNodeClientCerts = true);
    assertRejected(new EnableNodeToNodeClientCertsParams(), "already uses client certificates");
  }

  @Test
  public void testRetrySkipsTheAlreadyEnabledCheck() throws Exception {
    setupSupportedUniverse();
    updateUniverse(d -> d.getPrimaryCluster().userIntent.useNodeToNodeClientCerts = true);
    new EnableNodeToNodeClientCertsParams()
        .verifyParams(Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()), false);
  }

  @Test
  public void testRejectsOldVersion() throws Exception {
    setupSupportedUniverse();
    updateUniverse(
        d -> d.getPrimaryCluster().userIntent.ybSoftwareVersion = YB_SOFTWARE_VERSION_OLD);
    assertRejected(new EnableNodeToNodeClientCertsParams(), "requires YugabyteDB");
  }

  @Test
  public void testRejectsWithoutNodeToNodeEncryption() throws Exception {
    setupSupportedUniverse();
    updateUniverse(d -> d.getPrimaryCluster().userIntent.enableNodeToNodeEncrypt = false);
    assertRejected(new EnableNodeToNodeClientCertsParams(), "requires node-to-node encryption");
  }

  @Test
  public void testRejectsUnfinalizedSoftwareUpgrade() throws Exception {
    setupSupportedUniverse();
    updateUniverse(d -> d.softwareUpgradeState = SoftwareUpgradeState.PreFinalize);
    assertRejected(new EnableNodeToNodeClientCertsParams(), "software upgrade state");
  }

  @Test
  public void testRejectsRequiredUidGFlag() throws Exception {
    setupSupportedUniverse();
    updateUniverse(
        d ->
            d.getPrimaryCluster().userIntent.tserverGFlags =
                Map.of(GFlagsUtil.NODE_TO_NODE_REQUIRED_UID, "yugabyte"));
    assertRejected(new EnableNodeToNodeClientCertsParams(), GFlagsUtil.NODE_TO_NODE_REQUIRED_UID);
    assertFalse(
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .useNodeToNodeClientCerts);
  }
}
