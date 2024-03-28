// Copyright (c) YugaByte, Inc.

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
import java.util.List;
import java.util.stream.Collectors;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class YbcLogsComponentTest extends FakeDBApplication {
  @Mock public UniverseInfoHandler mockUniverseInfoHandler;
  @Mock public NodeUniverseManager mockNodeUniverseManager;
  @Mock public Config mockConfig;
  @Mock public SupportBundleUtil mockSupportBundleUtil = new SupportBundleUtil();

  private static final String testYbcLogsRegexPattern =
      "((?:.*)(?:yb-)(?:controller)(?:.*))(\\d{8})-(?:\\d*)\\.(?:.*)";
  private Universe universe;
  private Customer customer;
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-ybc_logs/";
  private String fakeSourceComponentPath = fakeSupportBundleBasePath + "ybc-data/";
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
    Path ybcTempFile =
        Paths.get(
            createTempFile(
                fakeSourceComponentPath + "contoller/logs/", "tmp.txt", "test-logs-content"));
    this.fakeTargetComponentPath = fakeBundlePath + "/" + node.nodeName + "/logs";

    // List of fake logs, simulates the absolute paths of files on the node server
    List<String> fakeLogsList =
        Arrays.asList(
            "/mnt/ybc-data/controller/logs/yb-controller.u-n1."
                + "yugabyte.log.INFO.20220127-072322.1542.gz",
            "/mnt/ybc-data/controller/logs/yb-controller.u-n1."
                + "yugabyte.log.WARNING.20220127-045422.9107.gz");

    // Mock all the invocations with fake data
    when(mockSupportBundleUtil.getDataDirPath(any(), any(), any(), any()))
        .thenReturn(fakeSupportBundleBasePath);
    when(mockConfig.getString("yb.support_bundle.ybc_logs_regex_pattern"))
        .thenReturn(testYbcLogsRegexPattern);
    when(mockSupportBundleUtil.extractFileTypeFromFileNameAndRegex(any(), any()))
        .thenCallRealMethod();
    when(mockSupportBundleUtil.extractDateFromFileNameAndRegex(any(), any())).thenCallRealMethod();
    when(mockSupportBundleUtil.filterFilePathsBetweenDates(any(), any(), any(), any()))
        .thenCallRealMethod();
    when(mockSupportBundleUtil.filterList(any(), any())).thenCallRealMethod();
    when(mockSupportBundleUtil.checkDateBetweenDates(any(), any(), any())).thenCallRealMethod();
    when(mockSupportBundleUtil.unGzip(any(), any())).thenCallRealMethod();
    when(mockSupportBundleUtil.unTar(any(), any())).thenCallRealMethod();
    doCallRealMethod()
        .when(mockSupportBundleUtil)
        .batchWiseDownload(
            any(), any(), any(), any(), any(), any(), any(), any(), any(), eq(false));

    when(mockNodeUniverseManager.checkNodeIfFileExists(any(), any(), any())).thenReturn(true);
    // Generate a fake shell response containing the entire list of file paths
    // Mocks the server response
    List<Path> fakeLogFilePathList =
        fakeLogsList.stream().map(Paths::get).collect(Collectors.toList());
    when(mockNodeUniverseManager.getNodeFilePaths(any(), any(), any(), eq(1), eq("f")))
        .thenReturn(fakeLogFilePathList);

    when(mockUniverseInfoHandler.downloadNodeFile(any(), any(), any(), any(), any(), any()))
        .thenAnswer(
            answer -> {
              Path fakeTargetComponentTarGzPath =
                  Paths.get(fakeTargetComponentPath, "tempOutput.tar.gz");
              Files.createDirectories(Paths.get(fakeTargetComponentPath));
              createTarGzipFiles(Arrays.asList(ybcTempFile), fakeTargetComponentTarGzPath);
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
    Date startDate = new Date(Long.MIN_VALUE);
    Date endDate = new Date(Long.MAX_VALUE);

    // Calling the download function
    YbcLogsComponent ybcLogsComponent =
        new YbcLogsComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockConfig, mockSupportBundleUtil);
    ybcLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, node);

    // Check that the download function is called
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), any(), any());

    // Check if the logs directory is created
    Boolean isDestDirCreated = new File(fakeTargetComponentPath).exists();
    assertTrue(isDestDirCreated);
  }
}
