// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.supportbundle;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.io.IOException;
import java.nio.file.Paths;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ApplicationLogsComponentTest extends FakeDBApplication {
  @Mock public BaseTaskDependencies mockBaseTaskDependencies;
  @Mock public Config mockConfig;

  private final SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd");
  private final String testRegexPattern = "application-log-\\d{4}-\\d{2}-\\d{2}\\.gz";
  private final String testSdfPattern = "'application-log-'yyyy-MM-dd'.gz'";

  private Universe universe;
  private Customer customer;
  public SupportBundleUtil mockSupportBundleUtil = new SupportBundleUtil();
  private String fakeSupportBundleBasePath = "/tmp/yugaware_tests/support_bundle-application_logs/";
  private String fakeSourceLogsPath = fakeSupportBundleBasePath + "logs/";
  private String fakeBundlePath =
      fakeSupportBundleBasePath + "yb-support-bundle-test-20220308000000.000-logs";

  @Before
  public void setUp() {
    // Setup fake temp log files, universe, customer
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());
    List<String> fakeLogsList =
        Arrays.asList(
            "application-log-2022-03-05.gz",
            "application-log-2022-03-06.gz",
            "application-log-2022-03-07.gz",
            "application-log-2022-03-08.gz",
            "application.log");
    for (String fileName : fakeLogsList) {
      File fakeFile = new File(fakeSourceLogsPath + fileName);
      if (!fakeFile.exists()) {
        createTempFile(fakeSourceLogsPath, fileName, "test-application-logs-content");
      }
    }

    // Mock all the config invocations with fake data
    when(mockBaseTaskDependencies.getConfig()).thenReturn(mockConfig);
    when(mockConfig.getString("log.override.path")).thenReturn(fakeSourceLogsPath);
    when(mockConfig.getString("yb.support_bundle.application_logs_regex_pattern"))
        .thenReturn(testRegexPattern);
    when(mockConfig.getString("yb.support_bundle.application_logs_sdf_pattern"))
        .thenReturn(testSdfPattern);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(fakeSupportBundleBasePath));
  }

  @Test
  public void testDownloadComponentBetweenDatesTillCurrentDay() throws IOException, ParseException {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-03-06");
    Date endDate = mockSupportBundleUtil.getTodaysDate();

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList =
        Arrays.asList(
            "application-log-2022-03-06.gz",
            "application-log-2022-03-07.gz",
            "application-log-2022-03-08.gz",
            "application.log");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesWithOlderDates() throws IOException, ParseException {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-03-06");
    Date endDate = dateFormat.parse("2022-03-07");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList =
        Arrays.asList("application-log-2022-03-06.gz", "application-log-2022-03-07.gz");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesPartialBounds() throws IOException, ParseException {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-03-01");
    Date endDate = dateFormat.parse("2022-03-05");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList = Arrays.asList("application-log-2022-03-05.gz");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesOutOfBounds() throws IOException, ParseException {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-03-01");
    Date endDate = dateFormat.parse("2022-03-03");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList = Arrays.asList();

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }
}
