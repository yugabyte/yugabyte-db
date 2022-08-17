package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.MockitoAnnotations.initMocks;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;

@RunWith(JUnitParamsRunner.class)
public class BackupUtilTest extends FakeDBApplication {

  private static final Map<String, String> REGION_LOCATIONS =
      new HashMap<String, String>() {
        {
          put("us-west1", "s3://backups.yugabyte.com/test/user/reg1");
          put("us-west2", "s3://backups.yugabyte.com/test/user/reg2");
          put("us-east1", "s3://backups.yugabyte.com/test/user/reg3");
        }
      };

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

  @SuppressWarnings("unused")
  private Object[] paramsToValidateFrequency() {
    return new Object[] {4800000L, 3600000L};
  }

  @Test(expected = Test.None.class)
  @Parameters(method = "paramsToValidateFrequency")
  public void testBackupFrequencyValid(Long frequency) {
    BackupUtil.validateBackupFrequency(frequency);
  }

  @SuppressWarnings("unused")
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
  private Object[] getBackupSuccessData() {
    String backupSuccessWithNoRegions = "backup/backup_success_with_no_regions.json";

    String backupSuccessWithRegions = "backup/backup_success_with_regions.json";

    return new Object[] {
      new Object[] {backupSuccessWithNoRegions, 0},
      new Object[] {backupSuccessWithRegions, 3}
    };
  }

  @Test
  @Parameters(method = "getBackupSuccessData")
  public void testExtractbackupLocations(String dataFile, int expectedCount) throws IOException {
    ObjectMapper mapper = new ObjectMapper();
    JsonNode locations = mapper.readTree(TestUtils.readResource(dataFile));
    List<BackupUtil.RegionLocations> actualLocations =
        BackupUtil.extractPerRegionLocationsFromBackupScriptResponse(locations);
    if (expectedCount == 0) {
      assertTrue(actualLocations.size() == 0);
    } else {
      Map<String, String> regionLocations = new HashMap<>();
      actualLocations.forEach(aL -> regionLocations.put(aL.REGION, aL.LOCATION));
      assertEquals(regionLocations.size(), REGION_LOCATIONS.size());
      assertTrue(regionLocations.equals(REGION_LOCATIONS));
    }
  }

  @Test
  @Parameters(value = {"s3://backup, s3://backup/foo, foo", "s3://backup/, s3://backup//foo, foo"})
  public void testGetBackupIdentifier(
      String configDefaultLocation, String defaultBackupLocation, String expectedIdentifier) {
    String actualIdentifier =
        BackupUtil.getBackupIdentifier(configDefaultLocation, defaultBackupLocation);
    assertEquals(expectedIdentifier, actualIdentifier);
  }

  @Test
  @Parameters(
      value = {
        "s3://backup/foo, s3://backup, s3://region/, s3://region//foo",
        "s3://backup/foo, s3://backup, s3://region, s3://region/foo",
        "s3://backup//foo, s3://backup/, s3://region, s3://region/foo"
      })
  public void getExactRegionLocation(
      String backupLocation,
      String configDefaultLocation,
      String configRegionLocation,
      String expectedRegionLocation) {
    String actualRegionLocation =
        BackupUtil.getExactRegionLocation(
            backupLocation, configDefaultLocation, configRegionLocation);
    assertEquals(expectedRegionLocation, actualRegionLocation);
  }
}
