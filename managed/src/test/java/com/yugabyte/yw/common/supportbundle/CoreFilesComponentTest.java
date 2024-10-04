// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.supportbundle;

import static com.yugabyte.yw.common.TestHelper.createTarGzipFiles;
import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.SupportBundle;
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
import java.util.List;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class CoreFilesComponentTest extends FakeDBApplication {
  @Mock public UniverseInfoHandler mockUniverseInfoHandler;
  @Mock public NodeUniverseManager mockNodeUniverseManager;
  @Mock public Config mockConfig;
  @Mock public SupportBundleUtil mockSupportBundleUtil = new SupportBundleUtil();
  @Captor public ArgumentCaptor<List<String>> captorSourceNodeFiles;

  private Universe universe;
  private Customer customer;
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-core_files/";
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
    Files.createDirectories(Paths.get(fakeBundlePath));

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
    Files.createDirectories(Paths.get(fakeSourceComponentPath + "cores/"));
    Path masterTempFile =
        Paths.get(
            createTempFile(
                fakeSourceComponentPath + "cores/", "tmp.txt", "test-core-files-content"));
    this.fakeTargetComponentPath = fakeBundlePath + "/" + node.nodeName + "/cores";

    // Mock all the invocations with fake data
    doCallRealMethod()
        .when(mockSupportBundleUtil)
        .downloadNodeLevelComponent(
            any(), any(), any(), any(), any(), any(), any(), any(), eq(true));
    doCallRealMethod()
        .when(mockSupportBundleUtil)
        .batchWiseDownload(any(), any(), any(), any(), any(), any(), any(), any(), any(), eq(true));
    List<Pair<Long, String>> fileSizeNameList =
        Arrays.asList(
            new Pair<>(101L, "core_test.2"),
            new Pair<>(15L, "core_test.3"),
            new Pair<>(100L, "core_test.1"),
            new Pair<>(10L, "core_test.4"));
    doReturn(fileSizeNameList)
        .when(mockNodeUniverseManager)
        .getNodeFilePathsAndSize(any(), any(), any(), any(), any());

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

    // Define the task params with the cores info.
    CoreFilesComponent coreFilesComponent =
        new CoreFilesComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockSupportBundleUtil);
    SupportBundle testSupportBundle =
        new SupportBundle(null, universe.getUniverseUUID(), null, null, null, null, null);
    SupportBundleFormData testFormData = new SupportBundleFormData();
    testFormData.maxNumRecentCores = 2;
    testFormData.maxCoreFileSize = 20;
    SupportBundleTaskParams testTaskParams =
        new SupportBundleTaskParams(testSupportBundle, testFormData, customer, universe);

    // Calling the download function
    coreFilesComponent.downloadComponentBetweenDates(
        testTaskParams, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, node);

    // Check that the download function is called
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), any(), any());

    // Check if the cores directory is created
    Boolean isDestDirCreated = new File(fakeTargetComponentPath).exists();
    assertTrue(isDestDirCreated);
  }

  @Test
  public void testDownloadComponentWithCoreFiltering() throws Exception {
    // Define any start and end dates to filter - doesn't matter as internally not used
    Date startDate = new Date();
    Date endDate = new Date();

    // Define the task params with the cores info.
    CoreFilesComponent coreFilesComponent =
        new CoreFilesComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockSupportBundleUtil);
    SupportBundle testSupportBundle =
        new SupportBundle(null, universe.getUniverseUUID(), null, null, null, null, null);
    SupportBundleFormData testFormData = new SupportBundleFormData();
    testFormData.maxNumRecentCores = 3;
    testFormData.maxCoreFileSize = 100;
    SupportBundleTaskParams testTaskParams =
        new SupportBundleTaskParams(testSupportBundle, testFormData, customer, universe);

    // Calling the download function
    coreFilesComponent.downloadComponentBetweenDates(
        testTaskParams, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, node);

    // Check that the download function is called
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), any(), any());

    // Check that there are only 2 core files that we are trying to collect even though there are 4
    // found on the DB node.
    verify(mockSupportBundleUtil)
        .downloadNodeLevelComponent(
            any(),
            any(),
            any(),
            any(),
            any(),
            any(),
            captorSourceNodeFiles.capture(),
            any(),
            eq(true));
    List<String> expectedSourceNodeFiles = Arrays.asList("core_test.3", "core_test.1");
    System.out.println("CAPTOR: " + captorSourceNodeFiles.getValue().toString());
    assertEquals(expectedSourceNodeFiles, captorSourceNodeFiles.getValue());

    // Check if the cores directory is created
    Boolean isDestDirCreated = new File(fakeTargetComponentPath).exists();
    assertTrue(isDestDirCreated);
  }
}
