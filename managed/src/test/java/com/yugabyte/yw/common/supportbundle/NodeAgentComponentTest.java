// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.supportbundle;

import static com.yugabyte.yw.common.TestHelper.createTarGzipFiles;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.stream.Collectors;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class NodeAgentComponentTest extends FakeDBApplication {
  @Mock public UniverseInfoHandler mockUniverseInfoHandler;
  @Mock public NodeUniverseManager mockNodeUniverseManager;
  @Mock public Config mockConfig;
  @Mock public SupportBundleUtil mockSupportBundleUtil = new SupportBundleUtil();
  @Mock public RuntimeConfGetter MockConfGetter;

  private Universe universe;
  private Customer customer;
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-universe_logs";
  private Path fakeBundlePath =
      Paths.get(fakeSupportBundleBasePath, "yb-support-bundle-test-20220308000000.000-logs");
  private String nodeAgentHome = fakeSupportBundleBasePath + "/node-agent";
  private String nodeAgentLogDir = nodeAgentHome + "/logs";
  private Path targetNodeComponentPath =
      fakeBundlePath.resolve(NodeAgentComponent.class.getSimpleName() + ".tar.gz");
  private NodeDetails node = new NodeDetails();
  private NodeAgent nodeAgent = new NodeAgent();

  @Before
  public void setUp() throws Exception {
    // Setup fake temp files, universe, customer
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());

    // Add a fake node to the universe with a node name
    node.nodeName = "node-1";
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.20.30.40";

    // Create node agent record.
    nodeAgent.setIp(node.cloudInfo.private_ip);
    nodeAgent.setName(node.nodeName);
    nodeAgent.setPort(9070);
    nodeAgent.setCustomerUuid(customer.getUuid());
    nodeAgent.setOsType(OSType.LINUX);
    nodeAgent.setArchType(ArchType.AMD64);
    nodeAgent.setVersion("2.19.2-b1");
    nodeAgent.setHome(nodeAgentHome);
    nodeAgent.setState(State.READY);
    nodeAgent.save();

    this.universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              universe.setUniverseDetails(universeDetails);
            });

    Path tempNodeAgentLogFile =
        Paths.get(createTempFile(nodeAgentLogDir, "node-agent.log", "test-logs-content"));

    // List of fake logs, simulates the absolute paths of files on the node server
    List<String> fakeLogsList =
        Arrays.asList(
            nodeAgentLogDir + "/node-agent.log",
            nodeAgentLogDir + "/node_agent-2023-08-07T22-50-15.473.log.gz");

    List<Path> fakeLogFilePathList =
        fakeLogsList.stream().map(Paths::get).collect(Collectors.toList());

    // Mock all the invocations with fake data
    when(mockSupportBundleUtil.unGzip(any(), any())).thenCallRealMethod();
    when(mockSupportBundleUtil.unTar(any(), any())).thenCallRealMethod();
    doCallRealMethod()
        .when(mockSupportBundleUtil)
        .batchWiseDownload(any(), any(), any(), any(), any(), any(), any(), any(), any());

    when(mockNodeUniverseManager.getNodeFilePaths(any(), any(), any(), eq(1), eq("f")))
        .thenReturn(fakeLogFilePathList);
    // Generate a fake shell response containing the output of the "check file exists" script
    // Mocks the server response as "file existing"
    when(mockNodeUniverseManager.checkNodeIfFileExists(any(), any(), any())).thenReturn(true);
    when(mockUniverseInfoHandler.downloadNodeFile(any(), any(), any(), any(), any(), any()))
        .thenAnswer(
            ans -> {
              Files.createDirectories(fakeBundlePath);
              createTarGzipFiles(Arrays.asList(tempNodeAgentLogFile), targetNodeComponentPath);
              return targetNodeComponentPath;
            });
  }

  @After
  public void tearDown() throws IOException {
    //  FileUtils.deleteDirectory(new File(fakeSupportBundleBasePath));
  }

  @Test
  public void testDownloadComponent() throws Exception {
    // Define any start and end dates to filter - doesn't matter as internally not used

    // Calling the download function
    NodeAgentComponent nodeAgentComponent =
        new NodeAgentComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockSupportBundleUtil);
    nodeAgentComponent.downloadComponent(null, customer, universe, fakeBundlePath, node);

    ArgumentCaptor<List<String>> sourceFiles = ArgumentCaptor.forClass(List.class);
    // Check that the download function is called.
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(
            any(),
            any(),
            any(),
            eq(fakeSupportBundleBasePath),
            sourceFiles.capture(),
            eq(targetNodeComponentPath));
    List<String> expectedSourceFiles =
        Arrays.asList(
            "node-agent/logs/node-agent.log",
            "node-agent/logs/node_agent-2023-08-07T22-50-15.473.log.gz");
    assertEquals(expectedSourceFiles, sourceFiles.getValue());
    // Check if the logs directory is created inside the bundle directory.
    assertTrue(fakeBundlePath.resolve("node-agent.log").toFile().exists());
    // The temporary compressed file must be deleted.
    assertFalse(targetNodeComponentPath.toFile().exists());
  }
}
