package com.yugabyte.yw.common;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.MockitoAnnotations.initMocks;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.services.YBClientService;
import java.util.Arrays;
import java.util.LinkedList;
import java.util.List;
import junitparams.JUnitParamsRunner;
import org.junit.Before;

import org.junit.runner.RunWith;
import org.junit.Test;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import org.mockito.InjectMocks;
import junitparams.Parameters;

@RunWith(JUnitParamsRunner.class)
public class BackupUtilTest extends FakeDBApplication {

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

    String validConfigData =
        "{\"AWS_ACCESS_KEY_ID\":\"ZZZZZ\",\"AWS_SECRET_ACCESS_KEY\":\"YYYYYY\",\"BACKUP_LOCATION\":\"s3://backups.yugabyte.com/test/username/common\",\"AWS_HOST_BASE\":\"s3.amazonaws.com\",\"REGION_LOCATIONS\":[{\"REGION\":\"eastus\",\"LOCATION\":\"s3://backups.yugabyte.com/test/username/1\"},{\"REGION\":\"eastus2\",\"LOCATION\":\"s3://backups.yugabyte.com/test/username/2\"},{\"REGION\":\"westus2\",\"LOCATION\":\"s3://backups.yugabyte.com/test/username/3\"}]}";
    List<String> validConfigLocations =
        Arrays.asList(
            "s3://backups.yugabyte.com/test/username/common",
            "s3://backups.yugabyte.com/test/username/1",
            "s3://backups.yugabyte.com/test/username/2",
            "s3://backups.yugabyte.com/test/username/3");

    String configDataWithBackupLocationMisssing =
        "{\"AWS_ACCESS_KEY_ID\":\"ZZZZZ\",\"AWS_SECRET_ACCESS_KEY\":\"YYYYYY\",\"AWS_HOST_BASE\":\"s3.amazonaws.com\",\"REGION_LOCATIONS\":[{\"REGION\":\"eastus\",\"LOCATION\":\"s3://backups.yugabyte.com/test/username/1\"},{\"REGION\":\"eastus2\",\"LOCATION\":\"s3://backups.yugabyte.com/test/username/2\"},{\"REGION\":\"westus2\",\"LOCATION\":\"s3://backups.yugabyte.com/test/username/3\"}]}";
    List<String> locations = new LinkedList<>();

    String configDataWithBackupLocationEmpty =
        "{\"AWS_ACCESS_KEY_ID\":\"ZZZZZ\",\"AWS_SECRET_ACCESS_KEY\":\"YYYYYY\",\"BACKUP_LOCATION\":\"\",\"AWS_HOST_BASE\":\"s3.amazonaws.com\",\"REGION_LOCATIONS\":[{\"REGION\":\"eastus\",\"LOCATION\":\"s3://backups.yugabyte.com/test/username/1\"},{\"REGION\":\"eastus2\",\"LOCATION\":\"s3://backups.yugabyte.com/test/username/2\"},{\"REGION\":\"westus2\",\"LOCATION\":\"s3://backups.yugabyte.com/test/username/3\"}]}";

    String configDataWithRegionLocationMisssing =
        "{\"AWS_ACCESS_KEY_ID\":\"ZZZZZ\",\"AWS_SECRET_ACCESS_KEY\":\"YYYYYY\",\"BACKUP_LOCATION\":\"s3://backups.yugabyte.com/test/username/common\",\"AWS_HOST_BASE\":\"s3.amazonaws.com\"}";
    List<String> backupLocations = Arrays.asList("s3://backups.yugabyte.com/test/username/common");

    String configDataWithRegionLocationEmpty =
        "{\"AWS_ACCESS_KEY_ID\":\"ZZZZZ\",\"AWS_SECRET_ACCESS_KEY\":\"YYYYYY\",\"BACKUP_LOCATION\":\"s3://backups.yugabyte.com/test/username/common\",\"AWS_HOST_BASE\":\"s3.amazonaws.com\",\"REGION_LOCATIONS\":[]}";

    return new Object[] {
      new Object[] {validConfigData, true, validConfigLocations},
      new Object[] {configDataWithBackupLocationMisssing, false, locations},
      new Object[] {configDataWithBackupLocationEmpty, false, locations},
      new Object[] {configDataWithRegionLocationMisssing, true, backupLocations},
      new Object[] {configDataWithRegionLocationEmpty, true, backupLocations}
    };
  }

  @Test
  @Parameters(method = "getStorageConfigData")
  public void testGetStorageLocationList(
      String data, boolean isValid, List<String> expectedLocations) throws Exception {

    ObjectMapper mapper = new ObjectMapper();
    JsonNode configData = mapper.readTree(data);

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
