package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.TestHelper;
import java.util.Date;
import java.util.Arrays;
import java.util.List;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import java.io.File;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.text.ParseException;
import org.junit.Test;
import org.junit.Before;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.MockitoAnnotations;

@RunWith(JUnitParamsRunner.class)
public class SupportBundleUtilTest extends FakeDBApplication {

  @InjectMocks SupportBundleUtil supportBundleUtil;

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
    List<String> outputList = supportBundleUtil.filterList(unfilteredList, testRegexPattern);
    assertEquals(outputList, filteredList);
  }

  @Test
  public void testDeleteFile() throws ParseException {
    Path tmpFilePath = Paths.get(TestHelper.createTempFile("sample-data-to-delete"));
    supportBundleUtil.deleteFile(tmpFilePath);
    boolean isDeleted = Files.notExists(tmpFilePath);
    assertTrue(isDeleted);
  }
}
