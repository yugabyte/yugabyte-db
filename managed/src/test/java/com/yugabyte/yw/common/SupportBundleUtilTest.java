// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.utils.Pair;
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
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.apache.commons.lang3.time.DateUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.MockitoAnnotations;

@RunWith(JUnitParamsRunner.class)
public class SupportBundleUtilTest extends FakeDBApplication {

  @InjectMocks SupportBundleUtil supportBundleUtil;
  public static final String universe_logs_regex_pattern =
      "((?:.*)(?:yb-)(?:master|tserver)(?:.*))(\\d{8}-\\d{6})\\.(?:.*)";
  public static final String postgres_logs_regex_pattern =
      "((?:.*)(?:postgresql)-)(\\d{4}-\\d{2}-\\d{2}_\\d{6})(?:.*)";
  public static final String connection_pooling_logs_regex_pattern =
      "((?:.*)(?:ysql-conn-mgr)-)(\\d{4}-\\d{2}-\\d{2}_\\d{6})(?:.*)";
  public static final String ybc_logs_regex_pattern =
      "((?:.*)(?:yb-)(?:controller(?:-server)?)(?:.*))(\\d{8}-\\d{6})(?:\\..*)?";

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
    List<String> outputList =
        supportBundleUtil.filterList(unfilteredList, Arrays.asList(testRegexPattern));
    assertEquals(outputList, filteredList);
  }

  @Test
  public void testFilterListWithCompiledPatterns() throws ParseException {
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
    List<String> outputList =
        supportBundleUtil.filterList(
            unfilteredList, Arrays.asList("application-log-\\d{4}-\\d{2}-\\d{2}\\.gz"));
    assertEquals(outputList, filteredList);
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
                + "yb-master.yb-dev-sahith-new-yb-master-1.root.log.WARNING.20221116-093807.24.gz",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2024-12-24_065828.log.117963.gz",
            "yb-controller-server.ip-10-9-84-145.us-west-2.compute.internal.log.INFO."
                + "20260511-094842",
            "/mnt/ybc-data/controller/logs/yb-controller.u-n1.yugabyte.log.INFO."
                + "20220127-072322.1542.gz");
    List<String> expectedFileTypeList =
        Arrays.asList(
            "/mnt/disk0/yb-data/tserver/logs/postgresql-",
            "/mnt/disk0/yb-data/tserver/logs/"
                + "yb-tserver.yb-dev-sahith-new-yb-tserver-1.root.log.INFO.",
            "/mnt/disk0/yb-data/master/logs/"
                + "yb-master.yb-dev-sahith-new-yb-master-1.root.log.WARNING.",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-",
            "yb-controller-server.ip-10-9-84-145.us-west-2.compute.internal.log.INFO.",
            "/mnt/ybc-data/controller/logs/yb-controller.u-n1.yugabyte.log.INFO.");

    for (int i = 0; i < fileNameList.size(); ++i) {
      List<String> fileRegexList =
          Arrays.asList(
              universe_logs_regex_pattern,
              postgres_logs_regex_pattern,
              connection_pooling_logs_regex_pattern,
              ybc_logs_regex_pattern);
      assertEquals(
          expectedFileTypeList.get(i),
          supportBundleUtil.extractFileTypeFromFileNameAndRegex(
              fileNameList.get(i), fileRegexList));
    }
  }

  @Test
  public void testExtractFileTypeFromFileNameAndCompiledPatterns() throws ParseException {
    String fileName = "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-16_093918.log.gz";
    List<String> filePatterns = Arrays.asList(postgres_logs_regex_pattern);

    assertEquals(
        "/mnt/disk0/yb-data/tserver/logs/postgresql-",
        supportBundleUtil.extractFileTypeFromFileNameAndRegex(fileName, filePatterns));
  }

  @Test
  public void testExtractDateFromFileNameAndRegex() throws ParseException {
    SimpleDateFormat pgTs = new SimpleDateFormat("yyyy-MM-dd_HHmmss");
    SimpleDateFormat ybTs = new SimpleDateFormat("yyyyMMdd-HHmmss");

    List<String> fileNameList =
        Arrays.asList(
            "ysql-conn-mgr-2024-12-24_065828.log",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2024-12-24_065828.log.117963.gz",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-01-01_000000.log.117900",
            "yb-controller-server.ip-10-9-84-145.us-west-2.compute.internal.log.INFO."
                + "20260511-094842",
            "/mnt/d0/ybc-data/controller/logs/yb-controller-server.ip-10-9-84-145.us-west-2."
                + "compute.internal.log.INFO.20260511-094842.gz",
            "/mnt/ybc-data/controller/logs/yb-controller.u-n1.yugabyte.log.INFO."
                + "20220127-072322.1542.gz",
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

    List<Date> expectedDates =
        Arrays.asList(
            pgTs.parse("2024-12-24_065828"),
            pgTs.parse("2024-12-24_065828"),
            pgTs.parse("2022-01-01_000000"),
            ybTs.parse("20260511-094842"),
            ybTs.parse("20260511-094842"),
            ybTs.parse("20220127-072322"),
            pgTs.parse("2022-11-16_093918"),
            pgTs.parse("2021-12-31_000000"),
            pgTs.parse("2020-01-01_000000"),
            ybTs.parse("20221116-093807"),
            ybTs.parse("20221116-093807"),
            ybTs.parse("20221116-093807"),
            ybTs.parse("20221116-093807"),
            ybTs.parse("20221117-093807"),
            ybTs.parse("20221117-093807"));

    for (int i = 0; i < fileNameList.size(); ++i) {
      List<String> fileRegexList =
          Arrays.asList(
              universe_logs_regex_pattern,
              postgres_logs_regex_pattern,
              connection_pooling_logs_regex_pattern,
              ybc_logs_regex_pattern);
      assertEquals(
          expectedDates.get(i),
          supportBundleUtil.extractDateFromFileNameAndRegex(fileNameList.get(i), fileRegexList));
    }
  }

  @Test
  public void testExtractDateTimeFromFileNameAndCompiledPatterns() throws ParseException {
    SimpleDateFormat ybTs = new SimpleDateFormat("yyyyMMdd-HHmmss");
    String fileName =
        "/mnt/disk0/yb-data/tserver/logs/"
            + "yb-tserver.yb-dev-sahith-new-yb-tserver-1.root.log.INFO.20221116-093807.24.gz";
    List<String> filePatterns = Arrays.asList(universe_logs_regex_pattern);

    assertEquals(
        ybTs.parse("20221116-093807"),
        supportBundleUtil.extractDateTimeFromFileNameAndRegex(fileName, filePatterns));
  }

  @Test
  public void testGetValidStartAndEndDatesPreservesCallerTime() throws Exception {
    Config config = ConfigFactory.parseString("yb.support_bundle.default_date_range = 7");
    SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");
    Date start = sdf.parse("2026-05-06 09:17:13");
    Date end = sdf.parse("2026-05-06 15:30:45");

    Pair<Date, Date> result = supportBundleUtil.getValidStartAndEndDates(config, start, end);

    assertEquals(start, result.getFirst());
    assertEquals(end, result.getSecond());
  }

  @Test
  public void testGetValidStartAndEndDatesDefaultRangeUsesCurrentTime() throws Exception {
    Config config = ConfigFactory.parseString("yb.support_bundle.default_date_range = 3");
    Date before = new Date();

    Pair<Date, Date> result = supportBundleUtil.getValidStartAndEndDates(config, null, null);

    Date after = new Date();
    assertFalse(result.getSecond().before(before));
    assertFalse(result.getSecond().after(after));
    assertEquals(DateUtils.addDays(result.getSecond(), -3), result.getFirst());
  }

  @Test
  public void testFilterFilePathsBetweenDatesWithTimeAwareRange() throws ParseException {
    SimpleDateFormat pgTs = new SimpleDateFormat("yyyy-MM-dd_HHmmss");
    SimpleDateFormat day = new SimpleDateFormat("yyyy-MM-dd");
    Date startDate = pgTs.parse("2022-11-18_140000");
    Date endDate = day.parse("2022-11-22");

    List<String> logPaths =
        Arrays.asList(
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-18_120000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-18_160000.log.gz",
            "/mnt/disk0/yb-data/tserver/logs/postgresql-2022-11-19_000000.log.gz");

    List<String> filtered =
        supportBundleUtil.filterFilePathsBetweenDates(
            logPaths, Arrays.asList(postgres_logs_regex_pattern), startDate, endDate);

    assertEquals(3, filtered.size());
    String pgLogPrefix = "/mnt/disk0/yb-data/tserver/logs/postgresql-";
    assertTrue(filtered.contains(pgLogPrefix + "2022-11-18_120000.log.gz"));
    assertTrue(filtered.contains(pgLogPrefix + "2022-11-18_160000.log.gz"));
    assertTrue(filtered.contains(pgLogPrefix + "2022-11-19_000000.log.gz"));
  }

  @Test
  public void testFilterFilePathsBetweenDatesWithCompiledPatterns() throws ParseException {
    SimpleDateFormat ybTs = new SimpleDateFormat("yyyyMMdd-HHmmss");
    Date startDate = ybTs.parse("20221120-000000");
    Date endDate = ybTs.parse("20221122-000000");
    String fileTypePrefix = "/mnt/disk0/yb-data/tserver/logs/yb-tserver.test.log.INFO.";
    List<String> logPaths =
        Arrays.asList(
            fileTypePrefix + "20221120-000001.00",
            fileTypePrefix + "20221119-235959.00",
            fileTypePrefix + "20221119-120000.00",
            fileTypePrefix + "20221118-000000.00");
    List<String> patterns = Arrays.asList(universe_logs_regex_pattern);

    List<String> filtered =
        supportBundleUtil.filterFilePathsBetweenDates(logPaths, patterns, startDate, endDate);

    assertEquals(
        Arrays.asList(fileTypePrefix + "20221120-000001.00", fileTypePrefix + "20221119-235959.00"),
        filtered);
  }

  @Test
  public void testFilterFilePathsBetweenDatesIncludesOnlyOnePreStartFile() throws ParseException {
    SimpleDateFormat ybTs = new SimpleDateFormat("yyyyMMdd-HHmmss");
    Date startDate = ybTs.parse("20221120-000000");
    Date endDate = ybTs.parse("20221122-000000");
    String fileTypePrefix = "/mnt/disk0/yb-data/tserver/logs/yb-tserver.test.log.INFO.";
    List<String> logPaths =
        Arrays.asList(
            fileTypePrefix + "20221120-000001.00",
            fileTypePrefix + "20221119-235959.00",
            fileTypePrefix + "20221119-120000.00",
            fileTypePrefix + "20221118-000000.00");

    List<String> filtered =
        supportBundleUtil.filterFilePathsBetweenDates(
            logPaths, Arrays.asList(universe_logs_regex_pattern), startDate, endDate);

    assertEquals(
        Arrays.asList(fileTypePrefix + "20221120-000001.00", fileTypePrefix + "20221119-235959.00"),
        filtered);
  }

  @Test
  public void testFilterFilePathsBetweenDatesYbcLogs() throws ParseException {
    SimpleDateFormat ybTs = new SimpleDateFormat("yyyyMMdd-HHmmss");
    Date startDate = ybTs.parse("20260511-094800");
    Date endDate = ybTs.parse("20260511-100000");
    List<String> logPaths =
        Arrays.asList(
            "/mnt/d0/ybc-data/controller/logs/yb-controller-server.host.log.INFO."
                + "20260511-094842",
            "/mnt/d0/ybc-data/controller/logs/yb-controller-server.host.log.INFO."
                + "20260511-093000.gz",
            "/mnt/d0/ybc-data/controller/logs/yb-controller-server.host.log.INFO."
                + "20260510-120000");

    List<String> filtered =
        supportBundleUtil.filterFilePathsBetweenDates(
            logPaths, Arrays.asList(ybc_logs_regex_pattern), startDate, endDate);

    assertEquals(
        Arrays.asList(
            "/mnt/d0/ybc-data/controller/logs/yb-controller-server.host.log.INFO."
                + "20260511-094842",
            "/mnt/d0/ybc-data/controller/logs/yb-controller-server.host.log.INFO."
                + "20260511-093000.gz"),
        filtered);
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
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2024-12-24_065828.log.117963.gz",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-11-18_065828.log.117963.gz",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-11-17_055828.log.117963.gz",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2020-01-02_065828.log.117963",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-11-22_065828.log.117963",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-11-20_000000.log.117963.gz",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-11-20_000001.log.117963",
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
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-11-18_065828.log.117963.gz",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-11-17_055828.log.117963.gz",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-11-20_000000.log.117963.gz",
            "/mnt/disk0/yb-data/tserver/logs/ysql-conn-mgr-2022-11-20_000001.log.117963",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221116-093807.24.gz",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221118-145526.23",
            "/mnt/disk0/yb-data/tserver/logs/yb-tserver.yb-dev-sahith-new-yb-tserver-1.root."
                + "log.INFO.20221121-000000.00.gz",
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

    List<String> filteredLogFilePaths =
        supportBundleUtil.filterFilePathsBetweenDates(
            unfilteredLogFilePaths,
            Arrays.asList(
                universe_logs_regex_pattern,
                postgres_logs_regex_pattern,
                connection_pooling_logs_regex_pattern),
            startDate,
            endDate);

    assertTrue(
        expectedLogFilePaths.containsAll(filteredLogFilePaths)
            && filteredLogFilePaths.containsAll(expectedLogFilePaths));
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
