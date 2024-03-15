// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.supportbundle;

import static com.yugabyte.yw.common.TestHelper.createTarGzipFiles;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
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
public class InstanceComponentTest extends FakeDBApplication {
  @Mock public UniverseInfoHandler mockUniverseInfoHandler;
  @Mock public NodeUniverseManager mockNodeUniverseManager;
  @Mock public Config mockConfig;
  @Mock public SupportBundleUtil mockSupportBundleUtil = new SupportBundleUtil();

  private Universe universe;
  private Customer customer;
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-instance/";
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
                fakeSourceComponentPath + "master/instance/", "tmp.txt", "test-instance-content"));
    this.fakeTargetComponentPath = fakeBundlePath + "/" + node.nodeName + "/error-files";

    // Mock all the invocations with fake data
    when(mockSupportBundleUtil.getDataDirPath(any(), any(), any(), any()))
        .thenReturn(fakeSupportBundleBasePath);

    doCallRealMethod()
        .when(mockSupportBundleUtil)
        .downloadNodeLevelComponent(any(), any(), any(), any(), any(), any(), any(), any());
    doCallRealMethod()
        .when(mockSupportBundleUtil)
        .batchWiseDownload(any(), any(), any(), any(), any(), any(), any(), any(), any());

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
    InstanceComponent instanceComponent =
        new InstanceComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockConfig, mockSupportBundleUtil);
    instanceComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, node);

    // Check that the download function is called
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), any(), any());

    // Check if the instance directory is created
    Boolean isDestDirCreated = new File(fakeTargetComponentPath).exists();
    assertTrue(isDestDirCreated);
  }
}
