// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.ConfigHelper.ConfigType;
import com.yugabyte.yw.common.NodeAgentManager.InstallerFiles;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.NodeAgentForm;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.DeployContext;
import com.yugabyte.yw.models.NodeAgent.DeployType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import java.nio.file.Path;
import java.nio.file.Paths;
import org.apache.commons.io.FileUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeAgentManagerTest extends FakeDBApplication {
  @Mock private Config mockAppConfig;
  @Mock private ConfigHelper mockConfigHelper;

  private CertificateHelper certificateHelper;
  private NodeAgentManager nodeAgentManager;
  private NodeAgentHandler nodeAgentHandler;
  private Customer customer;

  @Before
  public void setup() throws Exception {
    customer = ModelFactory.testCustomer();
    lenient()
        .when(mockAppConfig.getString(eq(AppConfigHelper.YB_STORAGE_PATH)))
        .thenReturn(TestHelper.TMP_PATH);
    lenient().when(mockAppConfig.getInt(eq("yb.tlsCertificate.root.expiryInYears"))).thenReturn(1);
    lenient()
        .when(mockAppConfig.getInt(eq("yb.tlsCertificate.server.maxLifetimeInYears")))
        .thenReturn(1);
    when(mockConfigHelper.getConfig(eq(ConfigType.SoftwareVersion)))
        .thenReturn(ImmutableMap.of("version", "2.13.0.0"));
    Path nodeAgentPackage = Paths.get("/tmp/node_agent-2.13.0.0-b12-linux-amd64.tar.gz");
    FileUtils.touch(nodeAgentPackage.toFile());
    when(mockAppConfig.getString(eq(NodeAgentManager.NODE_AGENT_RELEASES_PATH_PROPERTY)))
        .thenReturn(nodeAgentPackage.getParent().toString());
    lenient()
        .when(mockFileHelperService.createTempFile(anyString(), anyString()))
        .thenReturn(Paths.get("/tmp/node-agent-installer.sh"));
    certificateHelper = new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class));
    nodeAgentManager =
        spy(
            new NodeAgentManager(
                mockAppConfig, mockConfigHelper, certificateHelper, mockFileHelperService));
    doReturn(new byte[1]).when(nodeAgentManager).getInstallerScript();
    nodeAgentHandler =
        new NodeAgentHandler(mockCommissioner, nodeAgentManager, mock(NodeAgentClient.class));
    nodeAgentHandler.enableConnectionValidation(false);
  }

  private NodeAgent createReadyNodeAgent() {
    NodeAgentForm payload = new NodeAgentForm();
    payload.version = "2.13.0.0";
    payload.name = "node1";
    payload.ip = "10.20.30.40";
    payload.osType = OSType.LINUX.name();
    payload.archType = ArchType.AMD64.name();
    payload.home = "/home/yugabyte/node-agent";
    NodeAgent nodeAgent = nodeAgentHandler.register(customer.getUuid(), payload);
    payload.state = State.READY.name();
    return nodeAgentHandler.updateState(customer.getUuid(), nodeAgent.getUuid(), payload);
  }

  private DeployContext deployContext(DeployType deployType) {
    return DeployContext.builder().deployType(deployType).build();
  }

  @Test
  public void testGetInstallerFilesFullUpgrade() {
    NodeAgent nodeAgent = createReadyNodeAgent();
    nodeAgent.saveState(State.UPGRADE);

    InstallerFiles installerFiles =
        nodeAgentManager.getInstallerFiles(nodeAgent, deployContext(DeployType.FULL));

    assertNotNull(installerFiles.getPackagePath());
    assertNotNull(installerFiles.getCertDir());
    assertNotNull(installerFiles.getNewCertPath());
    assertEquals(6, installerFiles.getCopyFileInfos().size());
    assertTrue(
        installerFiles.getCopyFileInfos().stream()
            .anyMatch(
                info -> info.getTargetPath().endsWith(NodeAgentManager.NODE_AGENT_INSTALLER_FILE)));
  }

  @Test
  public void testGetInstallerFilesBinaryOnlyUpgrade() {
    NodeAgent nodeAgent = createReadyNodeAgent();

    InstallerFiles installerFiles =
        nodeAgentManager.getInstallerFiles(nodeAgent, deployContext(DeployType.BINARY_ONLY));

    assertNotNull(installerFiles.getPackagePath());
    assertNull(installerFiles.getCertDir());
    assertNull(installerFiles.getNewCertPath());
    assertEquals(2, installerFiles.getCopyFileInfos().size());
  }

  @Test
  public void testGetInstallerFilesCertsOnly() {
    NodeAgent nodeAgent = createReadyNodeAgent();
    nodeAgent.saveState(State.UPGRADE);

    InstallerFiles installerFiles =
        nodeAgentManager.getInstallerFiles(nodeAgent, deployContext(DeployType.CERTS_ONLY));

    assertNull(installerFiles.getPackagePath());
    assertNotNull(installerFiles.getCertDir());
    assertNotNull(installerFiles.getNewCertPath());
    assertEquals(4, installerFiles.getCopyFileInfos().size());
  }
}
