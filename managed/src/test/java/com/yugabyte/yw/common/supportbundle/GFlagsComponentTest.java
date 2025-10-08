// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static com.yugabyte.yw.common.TestHelper.createTarGzipFiles;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Date;
import java.util.HashSet;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class GFlagsComponentTest extends FakeDBApplication {
  @Mock public UniverseInfoHandler mockUniverseInfoHandler;
  @Mock public NodeUniverseManager mockNodeUniverseManager;
  @Mock public SupportBundleUtil mockSupportBundleUtil = new SupportBundleUtil();

  private Universe universe;
  private Customer customer;
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-gflags/";
  private String fakeSourceComponentPath = fakeSupportBundleBasePath + "yb-data/";
  private String fakeBundlePath =
      fakeSupportBundleBasePath + "yb-support-bundle-test-20220308000000.000-logs";
  private String fakeTargetComponentPath;
  private NodeDetails node = new NodeDetails();

  @Before
  public void setUp() throws Exception {
    // Setup fake temp files, universe, customer
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());

    // Add a fake node to the universe with a node name
    node.nodeName = "u-n1";
    this.universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              universe.setUniverseDetails(universeDetails);
            });

    // Create fake temp files to "download"
    Path masterTempFile =
        Paths.get(
            createTempFile(
                fakeSourceComponentPath + "master/conf/", "server.conf", "test-gflags-content"));
    this.fakeTargetComponentPath = fakeBundlePath + "/" + node.nodeName + "/conf";

    doCallRealMethod()
        .when(mockSupportBundleUtil)
        .downloadNodeLevelComponent(
            any(), any(), any(), any(), any(), any(), any(), any(), eq(false));
    doCallRealMethod()
        .when(mockSupportBundleUtil)
        .batchWiseDownload(
            any(), any(), any(), any(), any(), any(), any(), any(), any(), eq(false));

    // Mock all the invocations with fake data
    when(mockUniverseInfoHandler.downloadNodeFile(any(), any(), any(), any(), any(), any()))
        .thenAnswer(
            answer -> {
              Path fakeTargetComponentTarGzPath =
                  Paths.get(fakeTargetComponentPath, "tempOutput.tar.gz");
              Files.createDirectories(Paths.get(fakeTargetComponentPath));
              createTarGzipFiles(Arrays.asList(masterTempFile), fakeTargetComponentTarGzPath);
              return fakeTargetComponentTarGzPath;
            });
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(fakeSupportBundleBasePath));
  }

  @Test
  public void testDownloadComponentBetweenDates() throws Exception {
    // Define any start and end dates to filter - doesn't matter as internally not used
    Date startDate = new Date();
    Date endDate = new Date();

    // Calling the download function
    GFlagsComponent gFlagsComponent =
        new GFlagsComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockSupportBundleUtil);
    gFlagsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, node);

    // Check that the download function is called
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), any(), any());

    // Check if the gflags directory is created
    Boolean isDestDirCreated = new File(fakeTargetComponentPath).exists();
    assertTrue(isDestDirCreated);
  }

  @Test
  public void testDownloadComponentKubernetes() throws Exception {
    // Create a Kubernetes universe
    Universe k8sUniverse =
        ModelFactory.createUniverse("Test K8s Universe", customer.getId(), CloudType.kubernetes);

    // Add a fake node to the kubernetes universe with a node name
    NodeDetails k8sNode = new NodeDetails();
    k8sNode.nodeName = "k8s-n1";
    k8sUniverse =
        Universe.saveDetails(
            k8sUniverse.getUniverseUUID(),
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(k8sNode));
              universe.setUniverseDetails(universeDetails);
            });

    // Create fake temp files for Kubernetes path
    String k8sFakeSourcePath = fakeSupportBundleBasePath + "tmp/yugabyte/";
    Path k8sMasterTempFile =
        Paths.get(
            createTempFile(
                k8sFakeSourcePath + "master/conf/", "server.conf", "test-k8s-gflags-content"));
    String k8sFakeTargetPath = fakeBundlePath + "/" + k8sNode.nodeName + "/conf";

    // Mock the download for Kubernetes
    when(mockUniverseInfoHandler.downloadNodeFile(
            any(), any(), any(), eq(GFlagsUtil.K8S_HOME_DIR_FOR_GFLAG_OVERRIDES), any(), any()))
        .thenAnswer(
            answer -> {
              Path fakeTargetComponentTarGzPath = Paths.get(k8sFakeTargetPath, "tempOutput.tar.gz");
              Files.createDirectories(Paths.get(k8sFakeTargetPath));
              createTarGzipFiles(Arrays.asList(k8sMasterTempFile), fakeTargetComponentTarGzPath);
              return fakeTargetComponentTarGzPath;
            });

    // Calling the download function
    GFlagsComponent gFlagsComponent =
        new GFlagsComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockSupportBundleUtil);
    gFlagsComponent.downloadComponent(
        null, customer, k8sUniverse, Paths.get(fakeBundlePath), k8sNode);

    // Verify that the download function is called with the correct Kubernetes path
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(
            any(), any(), any(), eq(GFlagsUtil.K8S_HOME_DIR_FOR_GFLAG_OVERRIDES), any(), any());

    // Check if the gflags directory is created
    Boolean isDestDirCreated = new File(k8sFakeTargetPath).exists();
    assertTrue(isDestDirCreated);
  }
}
