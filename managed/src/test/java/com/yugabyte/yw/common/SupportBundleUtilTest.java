// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.MockitoAnnotations;

@RunWith(JUnitParamsRunner.class)
public class SupportBundleUtilTest extends FakeDBApplication {

  @InjectMocks SupportBundleUtil supportBundleUtil;
  public static final String universe_logs_regex_pattern =
      "((?:.*)(?:yb-)(?:master|tserver)(?:.*))(\\d{8})-(?:\\d*)\\.(?:.*)";
  public static final String postgres_logs_regex_pattern = "((?:.*)(?:postgresql)-)(.{10})(?:.*)";

  @Before
  public void setup() {
    MockitoAnnotations.initMocks(this);
  }

  @Test
  @Parameters({"2022-03-02,2022-03-07,5", "2021-12-28,2022-01-03,6"})
  public void testGetDateNDaysAgo(String dateBeforeStr, String dateAfterStr, String daysAgo)
      throws ParseException {
    SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd");
    Date dateBefore = sdf.parse(dateBeforeStr);
    Date dateAfter = sdf.parse(dateAfterStr);
    Date outputDate = supportBundleUtil.getDateNDaysAgo(dateAfter, Integer.parseInt(daysAgo));
    assertEquals(dateBefore, outputDate);
  }

  @Test
  @Parameters({
    "yb-support-bundle-random-universe-name-20220302112247.793-logs.tar.gz,2022-03-02",
    "yb-support-bundle-skurapati-gcp-uni-test-20210204152839.561-logs.tar.gz,2021-02-04"
  })
  public void testGetDateFromBundleFileName(String fileName, String dateStr) throws ParseException {
    SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd");
    Date fileDate = sdf.parse(dateStr);
    Date outputDate = supportBundleUtil.getDateFromBundleFileName(fileName);
    assertEquals(fileDate, outputDate);
  }

  @Test
  @Parameters({
    "2022-01-02,2022-01-02,2022-02-05",
    "2022-01-15,2022-01-02,2022-02-05",
    "2022-02-05,2022-01-02,2022-02-05"
  })
  public void testCheckDateBetweenDates(String dateStr1, String dateStr2, String dateStr3)
      throws ParseException {
    SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd");
    Date date1 = sdf.parse(dateStr1);
    Date date2 = sdf.parse(dateStr2);
    Date date3 = sdf.parse(dateStr3);
    boolean isBetween = supportBundleUtil.checkDateBetweenDates(date1, date2, date3);
    assertTrue(isBetween);
  }

  @Test
  public void testSortDatesWithPattern() throws ParseException {
    List<String> unsortedList =
        Arrays.asList(
            "2018-06-23", "2017-12-02", "2018-06-11", "2019-01-01", "2016-07-10", "2007-01-01");
    List<String> sortedList =
        Arrays.asList(
            "2007-01-01", "2016-07-10", "2017-12-02", "2018-06-11", "2018-06-23", "2019-01-01");
    List<String> outputList = supportBundleUtil.sortDatesWithPattern(unsortedList, "yyyy-MM-dd");
    assertEquals(outputList, sortedList);
  }

  @Test
  public void testFilterList() throws ParseException {
    String testRegexPattern = "application-log-\\d{4}-\\d{2}-\\d{2}\\.gz";
    List<String> unfilteredList =
        Arrays.asList(
            "application-log-2022-02-03.gz",
            "application-log-2022-02-04.gz",
            "application.log",
            "application-log-2022-02-05.gz");
    List<String> filteredList =
        Arrays.asList(
            "application-log-2022-02-03.gz",
            "application-log-2022-02-04.gz",
            "application-log-2022-02-05.gz");
    List<Path> outputList =
        supportBundleUtil.filterList(
            unfilteredList.stream().map(Paths::get).collect(Collectors.toList()),
            Arrays.asList(testRegexPattern));
    assertEquals(outputList, filteredList.stream().map(Paths::get).collect(Collectors.toList()));
  }

  @Test
  public void testDeleteFile() throws ParseException {
    Path tmpFilePath = Paths.get(TestHelper.createTempFile("sample-data-to-delete"));
    supportBundleUtil.deleteFile(tmpFilePath);
    boolean isDeleted = Files.notExists(tmpFilePath);
    assertTrue(isDeleted);
  }

  @Test
  public void testExtractFileTypeFromFileNameAndRegex() throws ParseException {
    List<String> fileNameList =
        Arrays.asList(
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-16_093918.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/"
                + "yb-tserver.yb-dev-sahith-new-yb-tserver-1.root.log.INFO.20221116-093807.24.gz",
            "/mnt/disk0/yb-data/master/logs/"
                + "yb-master.yb-dev-sahith-new-yb-master-1.root.log.WARNING.20221116-093807.24.gz");
    List<String> expectedFileTypeList =
        Arrays.asList(
            "/mnt/disk0/yb-data/tserver/logs/postgresql-",
            "/mnt/disk0/yb-data/tserver/logs/"
                + "yb-tserver.yb-dev-sahith-new-yb-tserver-1.root.log.INFO.",
            "/mnt/disk0/yb-data/master/logs/"
                + "yb-master.yb-dev-sahith-new-yb-master-1.root.log.WARNING.");

    for (int i = 0; i < fileNameList.size(); ++i) {
      List<String> fileRegexList =
          Arrays.asList(universe_logs_regex_pattern, postgres_logs_regex_pattern);
      assertEquals(
          expectedFileTypeList.get(i),
          supportBundleUtil.extractFileTypeFromFileNameAndRegex(
              fileNameList.get(i), fileRegexList));
    }
  }

  @Test
  public void testExtractDateFromFileNameAndRegex() throws ParseException {
    List<String> fileNameList =
        Arrays.asList(
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-16_093918.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2021-12-31_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2020-01-01_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/"
                + "yb-tserver.yb-dev-sahith-new-yb-tserver-1.root.log.INFO.20221116-093807.24.gz",
            "/mnt/disk0/yb-data/master/logs/"
                + "yb-master.yb-dev-sahith-new-yb-master-1.root.log.INFO.20221116-093807.24.gz",
            "/mnt/disk0/yb-data/tserver/logs/"
                + "yb-tserver.yb-dev-sahith-new-yb-tserver-1.root.log.WARNING.20221116-093807.24",
            "/mnt/disk0/yb-data/master/logs/"
                + "yb-master.yb-dev-sahith-new-yb-master-1.root.log.WARNING.20221116-093807.24",
            "/mnt/disk0/20221116/yb-data/master/logs/"
                + "yb-master.yb-dev-sahith-new-yb-master-1.root.log.WARNING.20221117-093807.24",
            "/mnt/disk0/2022-11-16/yb-data/master/logs/"
                + "yb-master.yb-dev-sahith-new-yb-master-1.root.log.WARNING.20221117-093807.24");

    List<String> expectedDateList =
        Arrays.asList(
            "2022-11-16",
            "2021-12-31",
            "2020-01-01",
            "2022-11-16",
            "2022-11-16",
            "2022-11-16",
            "2022-11-16",
            "2022-11-17",
            "2022-11-17");

    for (int i = 0; i < fileNameList.size(); ++i) {
      List<String> fileRegexList =
          Arrays.asList(universe_logs_regex_pattern, postgres_logs_regex_pattern);
      Date date = new SimpleDateFormat("yyyy-MM-dd").parse(expectedDateList.get(i));
      assertEquals(
          date,
          supportBundleUtil.extractDateFromFileNameAndRegex(fileNameList.get(i), fileRegexList));
    }
  }

  @Test
  public void testFilterFilePathsBetweenDates() throws ParseException {
    Date startDate = new SimpleDateFormat("yyyy-MM-dd").parse("2022-11-18");
    Date endDate = new SimpleDateFormat("yyyy-MM-dd").parse("2022-11-22");
    List<String> unfilteredLogFilePaths =
        Arrays.asList(
            "/mnt/disk1/yb-data/yb-data/tserver/logs/postgresql-2022-11-23_000000.log.gz",
            "/mnt/disk0/yb-data/yb-data/tserver/logs/postgresql-2022-11-15_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-16_093918.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-18_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-17_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-18_145526.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-19_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-20_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-21_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-22_000000.log",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221114-000000.00.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221116-000000.00.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221116-093807.24.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221121-000000.00.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221118-145526.23",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221116-093913.24.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221117-000000.01.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221117-000000.00.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221118-000000.00.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221118-145526.23",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221120-000000.00",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221122-000000.00",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221123-000000.00");

    List<String> expectedLogFilePaths =
        Arrays.asList(
            "/mnt/disk0/yb-data/yb-data/tserver/logs/postgresql-2022-11-15_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-17_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-18_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-18_145526.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-19_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-20_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-21_000000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-22_000000.log",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221116-000000.00.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221116-093807.24.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221118-145526.23",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221121-000000.00.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221117-000000.00.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221117-000000.01.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221118-000000.00.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221118-145526.23",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221120-000000.00",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.WARNING.20221122-000000.00");

    List<Path> unFilteredLogFilePathList =
        unfilteredLogFilePaths.stream().map(Paths::get).collect(Collectors.toList());
    List<Path> expectedLogFilePathList =
        expectedLogFilePaths.stream().map(Paths::get).collect(Collectors.toList());
    List<Path> filteredLogFilePaths =
        supportBundleUtil.filterFilePathsBetweenDates(
            unFilteredLogFilePathList,
            Arrays.asList(universe_logs_regex_pattern, postgres_logs_regex_pattern),
            startDate,
            endDate);

    assertTrue(
        expectedLogFilePathList.containsAll(filteredLogFilePaths)
            && filteredLogFilePaths.containsAll(expectedLogFilePathList));
  }

  @Test
  public void testGetServiceAccountName() throws ParseException {
    Provider testProvider =
        Provider.create(UUID.randomUUID(), Common.CloudType.kubernetes, "testProvider");

    Map<String, String> provConfig1 = new HashMap<String, String>();
    provConfig1.put("KUBECONFIG_SERVICE_ACCOUNT", "old service account");
    CloudInfoInterface.setCloudProviderInfoFromConfig(testProvider, provConfig1);
    assertEquals(
        "old service account",
        supportBundleUtil.getServiceAccountName(testProvider, mockKubernetesManager, null));

    Map<String, String> provConfig2 = new HashMap<String, String>();
    Map<String, String> envConfig = new HashMap<String, String>();

    CloudInfoInterface.setCloudProviderInfoFromConfig(testProvider, provConfig2);
    when(mockKubernetesManager.getKubeconfigUser(envConfig))
        .thenReturn("service-account-cluster-name");
    when(mockKubernetesManager.getKubeconfigCluster(envConfig)).thenReturn("cluster-name");
    assertEquals(
        "service-account",
        supportBundleUtil.getServiceAccountName(testProvider, mockKubernetesManager, envConfig));
  }
}
