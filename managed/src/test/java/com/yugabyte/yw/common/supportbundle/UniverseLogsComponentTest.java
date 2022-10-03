// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.supportbundle;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyBoolean;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
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
import java.util.Date;
import java.util.List;
import java.util.HashSet;
import java.util.Arrays;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseLogsComponentTest extends FakeDBApplication {
  @Mock public UniverseInfoHandler mockUniverseInfoHandler;
  @Mock public NodeUniverseManager mockNodeUniverseManager;
  @Mock public Config mockConfig;

  private final SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd");
  private final String testRegexPattern =
      "(?:(?:.*)(?:yb-)(?:master|tserver)(?:.*)(\\d{8})-(?:\\d*)\\.(?:.*))"
          + "|(?:(?:.*)(?:postgresql)-(.{10})(?:.*))";

  private Universe universe;
  private Customer customer;
  @Mock public SupportBundleUtil mockSupportBundleUtil = new SupportBundleUtil();
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-universe_logs/";
  private String fakeSourceLogsPath = fakeSupportBundleBasePath + "logs/";
  private String fakeBundlePath =
      fakeSupportBundleBasePath + "yb-support-bundle-test-20220308000000.000-logs";

  @Before
  public void setUp() throws ParseException {
    // Setup fake temp log files, universe, customer
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getCustomerId());

    // Add a fake node to the universe with a node name
    NodeDetails node = new NodeDetails();
    node.nodeName = "u-n1";
    this.universe =
        Universe.saveDetails(
            universe.universeUUID,
            (universe) -> {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              universeDetails.nodeDetailsSet = new HashSet<>(Arrays.asList(node));
              universe.setUniverseDetails(universeDetails);
            });

    // List of fake logs, simulates the absolute paths of files on the node server
    List<String> fakeLogsList =
        Arrays.asList(
            "/mnt/yb-data/master/logs/yb-master.u-n1.yugabyte.log.INFO.20220127-072322.1542.gz",
            "/mnt/yb-data/master/logs/yb-master.u-n1.yugabyte.log.WARNING.20220127-045422.9107.gz",
            "/mnt/yb-data/master/logs/yb-master.u-n1.yugabyte.log.WARNING.20220130-045422.9107.gz",
            "/mnt/yb-data/master/logs/yb-master.u-n1.yugabyte.log.INFO.20220127-045422.9107.gz",
            "/mnt/yb-data/master/logs/yb-master.u-n1.yugabyte.log.WARNING.20220127-072322.1542.gz",
            "/mnt/yb-data/master/logs/yb-master.u-n1.yugabyte.log.INFO.20220125-072322.1542.gz");

    // Mock all the invocations with fake data
    when(mockConfig.getString("yb.support_bundle.universe_logs_regex_pattern"))
        .thenReturn(testRegexPattern);
    when(mockSupportBundleUtil.getDataDirPath(any(), any(), any(), any()))
        .thenReturn(fakeSupportBundleBasePath);
    when(mockSupportBundleUtil.filterFilePathsBetweenDates(
            any(), any(), any(), any(), anyBoolean()))
        .thenCallRealMethod();
    lenient().when(mockSupportBundleUtil.getTodaysDate()).thenCallRealMethod();
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
    lenient()
        .when(mockUniverseInfoHandler.downloadNodeFile(any(), any(), any(), any(), any(), any()))
        .thenReturn(null);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(fakeSupportBundleBasePath));
  }

  @Test
  public void testDownloadComponentBetweenDatesWithPartialStartDateOverlap()
      throws IOException, ParseException {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-01-28");
    Date endDate = dateFormat.parse("2022-03-10");

    // Calling the download function
    UniverseLogsComponent universeLogsComponent =
        new UniverseLogsComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockConfig, mockSupportBundleUtil);
    universeLogsComponent.downloadComponentBetweenDates(
        customer, universe, Paths.get(fakeBundlePath), startDate, endDate);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList =
        Arrays.asList(
            "yb-master.u-n1.yugabyte.log.INFO.20220127-072322.1542.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220127-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220130-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.INFO.20220127-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220127-072322.1542.gz");

    // Capture the output list of file names to download after filtering
    ArgumentCaptor<String> captor = ArgumentCaptor.forClass(String.class);
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), captor.capture(), any());

    // Checking if the filtered list is same as expected list of files
    // Output is in the form of a string of file paths joined by semicolon(;)
    String actualOutput = captor.getValue();
    String[] files = actualOutput.split(";", 0);
    assertEquals(files.length, expectedFilesList.size() * 2);
    for (int i = 0; i < files.length; i++) {
      String fullPath = files[i];
      // Trim away the path to get only the file name
      String trimmedFileName = fullPath.substring(fullPath.lastIndexOf('/') + 1);
      assertTrue(expectedFilesList.contains(trimmedFileName));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesWithPartialEndDateOverlap()
      throws IOException, ParseException {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-01-20");
    Date endDate = dateFormat.parse("2022-01-28");

    // Calling the download function
    UniverseLogsComponent universeLogsComponent =
        new UniverseLogsComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockConfig, mockSupportBundleUtil);
    universeLogsComponent.downloadComponentBetweenDates(
        customer, universe, Paths.get(fakeBundlePath), startDate, endDate);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList =
        Arrays.asList(
            "yb-master.u-n1.yugabyte.log.INFO.20220127-072322.1542.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220127-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.INFO.20220127-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220127-072322.1542.gz",
            "yb-master.u-n1.yugabyte.log.INFO.20220125-072322.1542.gz");

    // Capture the output list of file names to download after filtering
    ArgumentCaptor<String> captor = ArgumentCaptor.forClass(String.class);
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), captor.capture(), any());

    // Checking if the filtered list is same as expected list of files
    // Output is in the form of a string of file paths joined by semicolon(;)
    String actualOutput = captor.getValue();
    String[] files = actualOutput.split(";", 0);
    assertEquals(files.length, expectedFilesList.size() * 2);
    for (int i = 0; i < files.length; i++) {
      String fullPath = files[i];
      // Trim away the path to get only the file name
      String trimmedFileName = fullPath.substring(fullPath.lastIndexOf('/') + 1);
      assertTrue(expectedFilesList.contains(trimmedFileName));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesWithSmallTimeFrame()
      throws IOException, ParseException {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-01-28");
    Date endDate = dateFormat.parse("2022-01-28");

    // Calling the download function
    UniverseLogsComponent universeLogsComponent =
        new UniverseLogsComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockConfig, mockSupportBundleUtil);
    universeLogsComponent.downloadComponentBetweenDates(
        customer, universe, Paths.get(fakeBundlePath), startDate, endDate);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList =
        Arrays.asList(
            "yb-master.u-n1.yugabyte.log.INFO.20220127-072322.1542.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220127-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.INFO.20220127-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220127-072322.1542.gz");

    // Capture the output list of file names to download after filtering
    ArgumentCaptor<String> captor = ArgumentCaptor.forClass(String.class);
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), captor.capture(), any());

    // Checking if the filtered list is same as expected list of files
    // Output is in the form of a string of file paths joined by semicolon(;)
    String actualOutput = captor.getValue();
    String[] files = actualOutput.split(";", 0);
    assertEquals(files.length, expectedFilesList.size() * 2);
    for (int i = 0; i < files.length; i++) {
      String fullPath = files[i];
      // Trim away the path to get only the file name
      String trimmedFileName = fullPath.substring(fullPath.lastIndexOf('/') + 1);
      assertTrue(expectedFilesList.contains(trimmedFileName));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesWithLargeTimeFrame()
      throws IOException, ParseException {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-01-20");
    Date endDate = dateFormat.parse("2022-02-5");

    // Calling the download function
    UniverseLogsComponent universeLogsComponent =
        new UniverseLogsComponent(
            mockUniverseInfoHandler, mockNodeUniverseManager, mockConfig, mockSupportBundleUtil);
    universeLogsComponent.downloadComponentBetweenDates(
        customer, universe, Paths.get(fakeBundlePath), startDate, endDate);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList =
        Arrays.asList(
            "yb-master.u-n1.yugabyte.log.INFO.20220127-072322.1542.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220127-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220130-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.INFO.20220127-045422.9107.gz",
            "yb-master.u-n1.yugabyte.log.WARNING.20220127-072322.1542.gz",
            "yb-master.u-n1.yugabyte.log.INFO.20220125-072322.1542.gz");

    // Capture the output list of file names to download after filtering
    ArgumentCaptor<String> captor = ArgumentCaptor.forClass(String.class);
    verify(mockUniverseInfoHandler, times(1))
        .downloadNodeFile(any(), any(), any(), any(), captor.capture(), any());

    // Checking if the filtered list is same as expected list of files
    // Output is in the form of a string of file paths joined by semicolon(;)
    String actualOutput = captor.getValue();
    String[] files = actualOutput.split(";", 0);
    assertEquals(files.length, expectedFilesList.size() * 2);
    for (int i = 0; i < files.length; i++) {
      String fullPath = files[i];
      // Trim away the path to get only the file name
      String trimmedFileName = fullPath.substring(fullPath.lastIndexOf('/') + 1);
      assertTrue(expectedFilesList.contains(trimmedFileName));
    }
  }
}
