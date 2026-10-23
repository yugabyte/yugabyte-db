// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static com.yugabyte.yw.common.TestHelper.createTempFile;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.io.File;
import java.io.IOException;
import java.nio.file.Paths;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.time.DateUtils;
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
  @Mock public RuntimeConfigFactory mockRuntimeConfigFactory;
  @Mock public Config mockGlobalRuntimeConfig;

  private final SimpleDateFormat dateFormat = new SimpleDateFormat("yyyy-MM-dd");
  private final SimpleDateFormat dateTimeFormat = new SimpleDateFormat("yyyy-MM-dd HH:mm");
  private final String testRegexPattern = "application-log-\\d{4}-\\d{2}-\\d{2}(\\.\\d+)?(\\.gz)?";
  private final String testSdfPattern = "'application-log-'yyyy-MM-dd";

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
    when(mockBaseTaskDependencies.getRuntimeConfigFactory()).thenReturn(mockRuntimeConfigFactory);
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockGlobalRuntimeConfig);
    when(mockConfig.getString("log.override.path")).thenReturn(fakeSourceLogsPath);
    when(mockGlobalRuntimeConfig.getString("yb.support_bundle.application_logs_regex_pattern"))
        .thenReturn(testRegexPattern);
    when(mockGlobalRuntimeConfig.getString("yb.support_bundle.application_logs_sdf_pattern"))
        .thenReturn(testSdfPattern);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(fakeSupportBundleBasePath));
  }

  /** Replaces the log files created by setUp, for tests that need a different set of names. */
  private void resetSourceLogs(String... fileNames) throws IOException {
    FileUtils.deleteDirectory(new File(fakeSourceLogsPath));
    for (String fileName : fileNames) {
      createTempFile(fakeSourceLogsPath, fileName, "test-application-logs-content");
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesTillCurrentDay() throws Exception {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-03-06");
    Date endDate = mockSupportBundleUtil.getTodaysDate();

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // Files expected to be present in the bundle after filtering.
    // "application-log-2022-03-05.gz" is the single overlap file collected before the start date.
    List<String> expectedFilesList =
        Arrays.asList(
            "application-log-2022-03-05.gz",
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
  public void testDownloadComponentBetweenDatesWithOlderDates() throws Exception {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-03-06");
    Date endDate = dateFormat.parse("2022-03-07");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // Files expected to be present in the bundle after filtering.
    // "application-log-2022-03-05.gz" is the single overlap file collected before the start date.
    List<String> expectedFilesList =
        Arrays.asList(
            "application-log-2022-03-05.gz",
            "application-log-2022-03-06.gz",
            "application-log-2022-03-07.gz");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesPartialBounds() throws Exception {
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
  public void testDownloadComponentBetweenDatesMidDayStart() throws Exception {
    // Start in the middle of a day. The start day is compared by date, so that whole day is in
    // the window and the file before it is the one collected as the overlap.
    Date startDate = dateTimeFormat.parse("2022-03-06 14:00");
    Date endDate = dateTimeFormat.parse("2022-03-07 18:00");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList =
        Arrays.asList(
            "application-log-2022-03-05.gz",
            "application-log-2022-03-06.gz",
            "application-log-2022-03-07.gz");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesWithinSingleDay() throws Exception {
    // Entire window falls within a single past day, so that day plus the file before it
    Date startDate = dateTimeFormat.parse("2022-03-06 10:00");
    Date endDate = dateTimeFormat.parse("2022-03-06 18:00");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // Files expected to be present in the bundle after filtering
    List<String> expectedFilesList =
        Arrays.asList("application-log-2022-03-05.gz", "application-log-2022-03-06.gz");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesIncludesOnlyOnePreStartFile() throws Exception {
    // Define start and end dates to filter
    Date startDate = dateFormat.parse("2022-03-08");
    Date endDate = dateFormat.parse("2022-03-08");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // Only the newest log file before the start date is expected, not all the older ones
    List<String> expectedFilesList =
        Arrays.asList("application-log-2022-03-07.gz", "application-log-2022-03-08.gz");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesIntraDayIncludesCurrentLog() throws Exception {
    // Window entirely within the current day, before any rollover has happened for it
    Date today = mockSupportBundleUtil.getTodaysDate();
    Date startDate = DateUtils.addHours(today, 9);
    Date endDate = DateUtils.addHours(today, 17);

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // The currently updated log file holds the requested window, plus the single overlap file
    List<String> expectedFilesList =
        Arrays.asList("application-log-2022-03-08.gz", "application.log");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesIndexedArchives() throws Exception {
    // Size based rollover puts several indexed archives on one date. Every index of an in-window
    // date is collected, but only the newest index of the date before the window.
    resetSourceLogs(
        "application-log-2022-03-05.0.gz",
        "application-log-2022-03-05.1.gz",
        "application-log-2022-03-06.0.gz",
        "application-log-2022-03-06.1.gz",
        "application-log-2022-03-06.2.gz",
        "application-log-2022-03-07.0.gz");
    Date startDate = dateFormat.parse("2022-03-06");
    Date endDate = dateFormat.parse("2022-03-07");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    List<String> expectedFilesList =
        Arrays.asList(
            "application-log-2022-03-05.1.gz",
            "application-log-2022-03-06.0.gz",
            "application-log-2022-03-06.1.gz",
            "application-log-2022-03-06.2.gz",
            "application-log-2022-03-07.0.gz");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesIndexComparedNumerically() throws Exception {
    // logback does not zero pad %i, so the index has to be compared as a number. A lexicographic
    // sort would pick .9 here.
    String[] archives = new String[11];
    for (int i = 0; i <= 10; i++) {
      archives[i] = String.format("application-log-2022-03-05.%d.gz", i);
    }
    resetSourceLogs(archives);
    Date startDate = dateFormat.parse("2022-03-06");
    Date endDate = dateFormat.parse("2022-03-07");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    List<String> expectedFilesList = Arrays.asList("application-log-2022-03-05.10.gz");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesMixedLegacyAndIndexed() throws Exception {
    // An upgraded node can hold both naming schemes. A legacy name counts as an older index than
    // any real one, so the indexed archive of the boundary date is the one collected.
    resetSourceLogs(
        "application-log-2022-03-05.gz",
        "application-log-2022-03-05.0.gz",
        "application-log-2022-03-06.0.gz");
    Date startDate = dateFormat.parse("2022-03-06");
    Date endDate = dateFormat.parse("2022-03-06");

    // Calling the download function
    ApplicationLogsComponent applicationLogsComponent =
        new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil);
    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    List<String> expectedFilesList =
        Arrays.asList("application-log-2022-03-05.0.gz", "application-log-2022-03-06.0.gz");

    // Checking if the filtered list is same as expected list of files
    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesSkipsFileRemovedAfterListing() throws Exception {
    // Size driven rollover means retention can delete an archive between the listing and the copy.
    // Losing that one file must not fail the whole component.
    Date startDate = dateFormat.parse("2022-03-06");
    Date endDate = dateFormat.parse("2022-03-07");

    ApplicationLogsComponent applicationLogsComponent =
        spy(new ApplicationLogsComponent(mockBaseTaskDependencies, mockSupportBundleUtil));
    Map<String, Long> listed = new HashMap<>();
    listed.put(fakeSourceLogsPath + "application-log-2022-03-06.gz", 100L);
    listed.put(fakeSourceLogsPath + "application-log-2022-03-99.gz", 100L);
    doReturn(listed)
        .when(applicationLogsComponent)
        .getFilesListWithSizes(any(), any(), any(), any(), any(), any());

    applicationLogsComponent.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), startDate, endDate, null);

    // The file that still exists is collected, the vanished one is simply absent.
    List<String> expectedFilesList = Arrays.asList("application-log-2022-03-06.gz");

    File[] files = new File(fakeBundlePath + "/application_logs/").listFiles();
    assertEquals(files.length, expectedFilesList.size());
    for (int i = 0; i < files.length; i++) {
      assertTrue(expectedFilesList.contains(files[i].getName()));
    }
  }

  @Test
  public void testDownloadComponentBetweenDatesOutOfBounds() throws Exception {
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
