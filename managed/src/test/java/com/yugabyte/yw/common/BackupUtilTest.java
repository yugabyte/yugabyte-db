package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.MockitoAnnotations.initMocks;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.services.YBClientService;
import java.util.Arrays;
import java.util.List;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;

@RunWith(JUnitParamsRunner.class)
public class BackupUtilTest extends FakeDBApplication {

  private static final List<String> CONFIG_LOCATIONS =
      Arrays.asList(
          "s3://backups.yugabyte.com/test/username/common",
          "s3://backups.yugabyte.com/test/username/1",
          "s3://backups.yugabyte.com/test/username/2",
          "s3://backups.yugabyte.com/test/username/3");

  @InjectMocks BackupUtil backupUtil;

  @Mock YBClientService ybService;

  @Before
  public void setup() {
    initMocks(this);
  }

  @Test(expected = Test.None.class)
  @Parameters({"0 */2 * * *", "0 */3 * * *", "0 */1 * * *", "5 */1 * * *", "1 * * * 2"})
  public void testBackupCronExpressionValid(String cronExpression) {
    BackupUtil.validateBackupCronExpression(cronExpression);
  }

  @Test
  @Parameters({"*/10 * * * *", "*/50 * * * *"})
  public void testBackupCronExpressionInvalid(String cronExpression) {
    Exception exception =
        assertThrows(
            PlatformServiceException.class,
            () -> BackupUtil.validateBackupCronExpression(cronExpression));
    assertEquals(
        "Duration between the cron schedules cannot be less than 1 hour", exception.getMessage());
  }

  private Object[] paramsToValidateFrequency() {
    return new Object[] {4800000L, 3600000L};
  }

  @Test(expected = Test.None.class)
  @Parameters(method = "paramsToValidateFrequency")
  public void testBackupFrequencyValid(Long frequency) {
    BackupUtil.validateBackupFrequency(frequency);
  }

  private Object[] paramsToInvalidateFrequency() {
    return new Object[] {1200000L, 2400000L};
  }

  @Test
  @Parameters(method = "paramsToInvalidateFrequency")
  public void testBackupFrequencyInvalid(Long frequency) {
    Exception exception =
        assertThrows(
            PlatformServiceException.class, () -> BackupUtil.validateBackupFrequency(frequency));
    assertEquals("Minimum schedule duration is 1 hour", exception.getMessage());
  }

  @SuppressWarnings("unused")
  private Object[] getStorageConfigData() {

    String validConfigData = "backup/storage_location_valid_config.json";

    String configDataWithBackupLocationMissing =
        "backup/storage_location_config_missing_location.json";

    String configDataWithBackupLocationEmpty = "backup/storage_location_config_empty_location.json";

    String configDataWithRegionLocationMissing =
        "backup/storage_location_config_missing_region_location.json";

    String configDataWithRegionLocationEmpty =
        "backup/storage_location_config_empty_region_location.json";

    return new Object[] {
      new Object[] {validConfigData, true, 4},
      new Object[] {configDataWithBackupLocationMissing, false, 0},
      new Object[] {configDataWithBackupLocationEmpty, false, 0},
      new Object[] {configDataWithRegionLocationMissing, true, 1},
      new Object[] {configDataWithRegionLocationEmpty, true, 1}
    };
  }

  @Test
  @Parameters(method = "getStorageConfigData")
  public void testGetStorageLocationList(
      String dataFile, boolean isValid, int expectedLocationsCount) throws Exception {
    ObjectMapper mapper = new ObjectMapper();
    JsonNode configData = mapper.readTree(TestUtils.readResource(dataFile));
    List<String> expectedLocations = CONFIG_LOCATIONS.subList(0, expectedLocationsCount);
    if (isValid) {
      List<String> actualLocations = backupUtil.getStorageLocationList(configData);
      assertEquals(expectedLocations.size(), actualLocations.size());
      for (String location : actualLocations) {
        assertTrue(expectedLocations.contains(location));
      }
    } else {
      try {
        List<String> actualLocations = backupUtil.getStorageLocationList(configData);
      } catch (PlatformServiceException ex) {
        assertTrue(true);
      }
    }
  }
}
