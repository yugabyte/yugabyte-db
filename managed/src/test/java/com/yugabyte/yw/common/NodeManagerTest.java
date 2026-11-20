// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.inject.Injector;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.ReplaceRootVolume;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.CertsRotateParams.CertRotationType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.ManagedLoadBalancerConfig;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancer;
import com.yugabyte.yw.models.helpers.ManagedLoadBalancerState;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.FileInputStream;
import java.io.InputStream;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import org.mockito.ArgumentCaptor;

public class NodeManagerTest extends FakeDBApplication {

  @Rule public TemporaryFolder tmp = new TemporaryFolder();

  private NodeManager nodeManager;
  private Customer customer;
  private Universe universe;

  @Before
  public void setUp() {
    // FakeDBApplication binds NodeManager to a mock, so build a real one around the mocked shell.
    nodeManager = new NodeManager();
    app.injector().instanceOf(Injector.class).injectMembers(nodeManager);
    when(mockShellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenReturn(ShellResponse.create(0, ""));

    customer = ModelFactory.testCustomer();
    Provider provider = ModelFactory.ociProvider(customer);
    UserIntent userIntent = ApiUtils.getDefaultUserIntentSingleAZ(provider);
    universe = ModelFactory.createUniverse(customer.getId());
    universe =
        Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
  }

  @Test
  public void testReplaceRootVolumeOciForced() {
    List<String> command = runOciReplaceRootVolume(true);
    assertTrue(command.toString(), command.contains("--force_replacement"));
  }

  @Test
  public void testReplaceRootVolumeOciNotForced() {
    List<String> command = runOciReplaceRootVolume(false);
    assertFalse(command.toString(), command.contains("--force_replacement"));
  }

  @Test
  public void testClientCertThatYbaSignsAnswersForTheLoadBalancer() throws Exception {
    Config config = spy(app.config());
    doReturn(tmp.getRoot().getAbsolutePath()).when(config).getString("yb.storage.path");
    UUID ca =
        new CertificateHelper(app.injector().instanceOf(RuntimeConfGetter.class))
            .createRootCA(config, "lb-certs", customer.getUuid());
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.rootCA = ca;
              details.rootAndClientRootCASame = true;
              UserIntent userIntent = details.getPrimaryCluster().userIntent;
              userIntent.enableClientToNodeEncrypt = true;
              ManagedLoadBalancerConfig lbConfig = new ManagedLoadBalancerConfig();
              lbConfig.setEnablePrivate(true);
              lbConfig.setDnsNames(List.of("*.db.example.com"));
              userIntent.setManagedLoadBalancer(lbConfig);
              ManagedLoadBalancerState state = new ManagedLoadBalancerState();
              state.put(
                  new ManagedLoadBalancer(
                      details.getPrimaryCluster().uuid,
                      UUID.randomUUID(),
                      ManagedLoadBalancer.Scheme.PRIVATE,
                      List.of(node.azUuid),
                      "lbi-test",
                      "10.150.0.5"));
              details.setManagedLoadBalancerState(state);
            });
    // Read the cert while the command runs: YBA deletes the temp copy after the copy.
    Set<String> sans = new HashSet<>();
    when(mockShellProcessHandler.run(anyList(), any(ShellProcessContext.class)))
        .thenAnswer(
            invocation -> {
              List<String> command = invocation.getArgument(0);
              String certPath =
                  command.get(command.indexOf("--server_cert_path_client_to_server") + 1);
              try (InputStream in = new FileInputStream(certPath)) {
                X509Certificate cert =
                    (X509Certificate)
                        CertificateFactory.getInstance("X.509").generateCertificate(in);
                cert.getSubjectAlternativeNames().forEach(san -> sans.add(san.get(1).toString()));
              }
              return ShellResponse.create(0, "");
            });

    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = node.nodeName;
    params.azUuid = node.azUuid;
    params.placementUuid = node.placementUuid;
    params.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    params.type = UpgradeTaskParams.UpgradeTaskType.Certs;
    params.setProperty("processType", ServerType.TSERVER.toString());
    params.certRotateAction = NodeManager.CertRotateAction.ROTATE_CERTS;
    params.enableClientToNodeEncrypt = true;
    params.rootCA = ca;
    params.rootAndClientRootCASame = true;
    params.rootCARotationType = CertRotationType.None;
    params.clientRootCARotationType = CertRotationType.ServerCert;
    nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);

    assertEquals(Set.of(node.cloudInfo.private_ip, "10.150.0.5", "*.db.example.com"), sans);
  }

  private List<String> runOciReplaceRootVolume(boolean force) {
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    ReplaceRootVolume.Params params = new ReplaceRootVolume.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = node.nodeName;
    params.azUuid = node.azUuid;
    params.replacementDisk = "target-image";
    params.forceReplacement = force;
    nodeManager.nodeCommand(NodeManager.NodeCommandType.Replace_Root_Volume, params);

    @SuppressWarnings("unchecked")
    ArgumentCaptor<List<String>> captor = ArgumentCaptor.forClass(List.class);
    verify(mockShellProcessHandler).run(captor.capture(), any(ShellProcessContext.class));
    List<String> command = captor.getValue();
    int diskIdx = command.indexOf("--replacement_disk");
    assertTrue(command.toString(), diskIdx >= 0);
    assertEquals("target-image", command.get(diskIdx + 1));
    return command;
  }
}
