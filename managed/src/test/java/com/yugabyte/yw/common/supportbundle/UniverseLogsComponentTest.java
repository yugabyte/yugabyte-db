// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.supportbundle;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static com.yugabyte.yw.common.TestHelper.createTarGzipFiles;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.io.IOException;
import java.nio.file.Paths;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Date;
import java.util.List;
import java.util.HashSet;
import java.util.Arrays;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseLogsComponentTest extends FakeDBApplication {
  @Mock public UniverseInfoHandler mockUniverseInfoHandler;
  @Mock public NodeUniverseManager mockNodeUniverseManager;
  @Mock public Config mockConfig;
  @Mock public SupportBundleUtil mockSupportBundleUtil = new SupportBundleUtil();

  private final String testRegexPattern =
      "(?:(?:.*)(?:yb-)(?:master|tserver)(?:.*)(\\d{8})-(?:\\d*)\\.(?:.*))"
          + "|(?:(?:.*)(?:postgresql)-(.{10})(?:.*))";
  private Universe universe;
  private Customer customer;
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-universe_logs/";
  private String fakeSourceComponentPath = fakeSupportBundleBasePath + "yb-data/";
  private String fakeBundlePath =
      fakeSupportBundleBasePath + "yb-support-bundle-test-20220308000000.000-logs";
  private String fakeTargetComponentPath;
  private NodeDetails node = new NodeDetails();

  @Before
  public void setUp() throws Exception {
    // Setup fake temp files, universe, customer
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getCustomerId());

    // Add a fake node to the universe with a node name
    node.nodeName = "u-n1";
    this.universe =
        Universe.saveDetails(
            universe.universeUUID,
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              universe.setUniverseDetails(universeDetails);
            });

    // Create fake temp files to "download"
    Path masterTempFile =
        Paths.get(
            createTempFile(
                fakeSourceComponentPath + "master/logs/", "tmp.txt", "test-logs-content"));
    this.fakeTargetComponentPath = fakeBundlePath + "/" + node.nodeName + "/logs";

    // List of fake logs, simulates the absolute paths of files on the node server
    List<String> fakeLogsList =
        Arrays.asList(
            "/mnt/yb-data/master/logs/yb-master.u-n1.yugabyte.log.INFO.20220127-072322.1542.gz",
            "/mnt/yb-data/master/logs/yb-master.u-n1.yugabyte.log.WARNING.20220127-045422.9107.gz");

    // Mock all the invocations with fake data
    when(mockSupportBundleUtil.getDataDirPath(any(), any(), any(), any()))
        .thenReturn(fakeSupportBundleBasePath);
    when(mockConfig.getString("yb.support_bundle.universe_logs_regex_pattern"))
        .thenReturn(testRegexPattern);
    when(mockSupportBundleUtil.filterFilePathsBetweenDates(
            any(), any(), any(), any(), anyBoolean()))
        .thenCallRealMethod();
    // lenient().when(mockSupportBundleUtil.getTodaysDate()).thenCallRealMethod();
    when(mockSupportBundleUtil.filterList(any(), any())).thenCallRealMethod();
    when(mockSupportBundleUtil.checkDateBetweenDates(any(), any(), any())).thenCallRealMethod();

    // Generate a fake shell response containing the entire list of file paths
    // Mocks the server response
    String fakeShellOutput = "Command output:\n" + String.join("\n", fakeLogsList);
    ShellResponse fakeShellResponse = ShellResponse.create(0, fakeShellOutput);
    when(mockNodeUniverseManager.runCommand(any(), any(), any())).thenReturn(fakeShellResponse);
    // Generate a fake shell response containing the output of the "check file exists" script
    // Mocks the server response as "file existing"
    String fakeShellRunScriptOutput = "Command output:\n1";
    ShellResponse fakeShellRunScriptResponse = ShellResponse.create(0, fakeShellRunScriptOutput);
    when(mockNodeUniverseManager.runScript(any(), any(), any(), any()))
        .thenReturn(fakeShellRunScriptResponse);

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
    Date startDate = new Date(Long.MIN_VALUE);
    Date endDate = new Date(Long.MAX_VALUE);

    // Calling the download function
    UniverseLogsComponent universeLogsComponent =
        new UniverseLogsComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockConfig, mockSupportBundleUtil);
    universeLogsComponent.downloadComponentBetweenDates(
        customer, universe, Paths.get(fakeBundlePath), startDate, endDate, node);

    // Check that the download function is called
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), any(), any());

    // Check if the logs directory is created
    Boolean isDestDirCreated = new File(fakeTargetComponentPath).exists();
    assertTrue(isDestDirCreated);
  }
}
