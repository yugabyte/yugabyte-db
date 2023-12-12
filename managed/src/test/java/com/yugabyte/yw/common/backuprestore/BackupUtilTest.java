package com.yugabyte.yw.common.backuprestore;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.MockitoAnnotations.initMocks;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupVersion;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Spy;
import play.libs.Json;

@Slf4j
@RunWith(JUnitParamsRunner.class)
public class BackupUtilTest extends FakeDBApplication {

  private Customer testCustomer;
  private Universe testUniverse;
  private final String DEFAULT_UNIVERSE_UUID = "univ-00000000-0000-0000-0000-000000000000";
  private static final Map<String, String> REGION_LOCATIONS =
      new HashMap<String, String>() {
        {
          put("us-west1", "s3://backups.yugabyte.com/test/user/reg1");
          put("us-west2", "s3://backups.yugabyte.com/test/user/reg2");
          put("us-east1", "s3://backups.yugabyte.com/test/user/reg3");
        }
      };

  @Spy @InjectMocks BackupUtil backupUtil;

  @Mock YBClientService ybService;

  @Before
  public void setup() {
    initMocks(this);
    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse(testCustomer.getId());
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
      new Object[] {backupSuccessWithNoRegions, 0}, new Object[] {backupSuccessWithRegions, 3}
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
  @Parameters(
      value = {
        "/tmp/nfs/yugabyte_backup/, /ybc_backup-foo/bar",
        "/yugabyte_backup/, /ybc_backup-foo/bar",
        "s3://backup/, /ybc_backup-foo/bar",
        "s3://backup/test/, /ybc_backup-foo/bar",
        "/tmp/nfs/yugabyte_backup/, /backup-foo/bar",
        "/yugabyte_backup/, /backup-foo/bar",
        "/yugabyte_backup/yugabyte_backup/, /backup-foo" + "/bar",
        "s3://backup/, /backup-foo/bar",
        "gs://backup/test/, /backup-foo/bar",
        "https://test.blob.windows.net/backup/, " + "/backup-foo/bar",
        "https://test.blob.windows.net/backup/, " + "/ybc_backup-foo/bar"
      })
  public void testGetBackupIdentifierWithNfsCheck(
      String defaultBackupLocationPrefix, String defaultBackupLocationSuffix) {
    String defaultBackupLocation =
        defaultBackupLocationPrefix + DEFAULT_UNIVERSE_UUID + defaultBackupLocationSuffix;
    String actualIdentifier = BackupUtil.getBackupIdentifier(defaultBackupLocation, true);
    assertTrue(actualIdentifier.startsWith("univ-00000000-0000-0000-0000-000000000000/"));
  }

  @Test
  @Parameters(
      value = {
        "/tmp/nfs/yugabyte_backup/, /ybc_backup-foo/bar" + ", true",
        "/yugabyte_backup/, /ybc_backup-foo/bar, true",
        "s3://backup/, /ybc_backup-foo/bar, false",
        "s3://backup/test/, /ybc_backup-foo/bar, false",
        "/tmp/nfs/yugabyte_backup/, /backup-foo/bar, false",
        "/yugabyte_backup/, /backup-foo/bar, false",
        "/yugabyte_backup/, /ybc_backup-foo/bar, true",
        "/yugabyte_backup/yugabyte_backup/, /backup-foo/bar" + ", false",
        "/yugabyte_backup/yugabyte_backup/, /ybc_backup-foo" + "/bar, true",
        "s3://backup/, /backup-foo/bar, false",
        "gs://backup/test/, /backup-foo/bar, false",
        "https://test.blob.windows.net/backup/, /backup-foo" + "/bar, false",
        "https://test.blob.windows.net/backup/, " + "/ybc_backup-foo/bar, false"
      })
  public void testGetBackupIdentifierWithoutNfsCheck(
      String defaultBackupLocationPrefix, String defaultBackupLocationSuffix, boolean expectedNfs) {
    String defaultBackupLocation =
        defaultBackupLocationPrefix + DEFAULT_UNIVERSE_UUID + defaultBackupLocationSuffix;
    String actualIdentifier =
        BackupUtil.getBackupIdentifier(defaultBackupLocation, false, "yugabyte_backup");
    if (expectedNfs) {
      assertTrue(
          actualIdentifier.startsWith(
              "yugabyte_backup/univ-00000000-0000-0000-0000-000000000000/"));
    } else {
      assertTrue(actualIdentifier.startsWith("univ-00000000-0000-0000-0000-000000000000/"));
    }
  }

  @Test
  @Parameters(
      value = {
        "s3://backup/, /ybc_backup-foo/bar, s3://region," + " s3://region/, /ybc_backup-foo/bar",
        "s3://backup/, /backup-foo/bar, s3://region," + " s3://region/, /backup-foo/bar",
        "s3://backup/, /ybc_backup-foo/bar, s3://region/," + " s3://region/, /ybc_backup-foo/bar",
        "s3://yugabyte_backup/, /ybc_backup-foo/bar,"
            + " s3://region/, s3://region/, /ybc_backup-foo"
            + "/bar",
        "s3://yugabyte_backup/, /backup-foo/bar, " + "s3://region/, s3://region/, /backup-foo/bar",
        "/backup/, /ybc_backup-foo/bar, /region/, " + "/region/, /ybc_backup-foo/bar",
        "/yugabyte_backup/, /ybc_backup-foo/bar, /region/, "
            + "/region/yugabyte_backup/, /ybc_backup-foo"
            + "/bar",
        "/yugabyte_backup/, /backup-foo/bar, /region/, " + "/region/, /backup-foo/bar",
        "/yugabyte_backup/yugabyte_backup/, /ybc_backup-foo"
            + "/bar, /region/, /region/yugabyte_backup/, "
            + "/ybc_backup-foo/bar",
        "/yugabyte_backup/yugabyte_backup/, /backup-foo/bar"
            + ", /region/, /region/, /backup-foo/bar"
      })
  public void testGetExactRegionLocation(
      String defaultBackupLocationPrefix,
      String defaultBackupLocationSuffix,
      String configRegionLocation,
      String expectedRegionLocationPrefix,
      String expectedRegionLocationSuffix) {
    String backupLocation =
        defaultBackupLocationPrefix + DEFAULT_UNIVERSE_UUID + defaultBackupLocationSuffix;
    String actualRegionLocation =
        BackupUtil.getExactRegionLocation(backupLocation, configRegionLocation, "yugabyte_backup");
    String expectedRegionLocation =
        expectedRegionLocationPrefix + DEFAULT_UNIVERSE_UUID + expectedRegionLocationSuffix;
    assertEquals(expectedRegionLocation, actualRegionLocation);
  }

  @Test
  @Parameters(value = {"true, true", "true, false", "false, false", "false, true"})
  public void testBackupLocationFormat(boolean emptyTableList, boolean isYbc) {
    BackupTableParams tableParams = new BackupTableParams();
    tableParams.setUniverseUUID(UUID.randomUUID());
    tableParams.backupUuid = UUID.randomUUID();
    tableParams.baseBackupUUID = tableParams.backupUuid;
    tableParams.backupParamsIdentifier = UUID.randomUUID();
    tableParams.setKeyspace("foo");
    if (emptyTableList) {
      tableParams.tableUUIDList = null;
    } else {
      tableParams.tableUUIDList = new ArrayList<>();
    }
    SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");
    String backupLocationTS = tsFormat.format(new Date());
    String formattedLocation =
        BackupUtil.formatStorageLocation(tableParams, isYbc, BackupVersion.V2, backupLocationTS);
    if (isYbc) {
      assertTrue(formattedLocation.contains("/ybc_backup"));
      if (emptyTableList) {
        assertTrue(formattedLocation.contains("/keyspace-foo"));
      } else {
        assertTrue(formattedLocation.contains("/multi-table-foo"));
      }
    } else {
      assertTrue(formattedLocation.contains("/backup"));
      if (emptyTableList) {
        assertTrue(formattedLocation.contains("/keyspace-foo"));
      } else {
        assertTrue(formattedLocation.contains("/multi-table-foo"));
      }
    }
  }

  @SuppressWarnings("unused")
  private Object[] updateStorageLocationParams() {

    JsonNode s3FormDataWithSlash =
        Json.parse(
            "{\"configName\": \""
                + "test-S3_1"
                + "\", \"name\": \"S3\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
                + " \"s3://def_bucket/default/\","
                + " \"AWS_ACCESS_KEY_ID\": \"A-KEY\", \"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\"}}");

    JsonNode s3FormDataNoSlash =
        Json.parse(
            "{\"configName\": \""
                + "test-S3_2"
                + "\", \"name\": \"S3\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
                + " \"s3://def_bucket/default\","
                + " \"AWS_ACCESS_KEY_ID\": \"A-KEY\", \"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\"}}");

    JsonNode nfsFormData =
        Json.parse(
            "{\"configName\": \""
                + "test-NFS_1"
                + "\", \"name\": \"NFS\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
                + " \"/tmp/nfs\"}}");

    JsonNode nfsFormDataSlash =
        Json.parse(
            "{\"configName\": \""
                + "test-NFS_2"
                + "\", \"name\": \"NFS\","
                + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
                + " \"/\"}}");

    return new Object[] {s3FormDataWithSlash, s3FormDataNoSlash, nfsFormData, nfsFormDataSlash};
  }

  @Test
  @Parameters(method = "updateStorageLocationParams")
  public void testUpdateDefaultStorageLocationWithoutYbc(JsonNode formData) {
    CustomerConfig testConfig = CustomerConfig.createWithFormData(testCustomer.getUuid(), formData);
    BackupTableParams tableParams = new BackupTableParams();
    tableParams.storageConfigUUID = testConfig.getConfigUUID();
    tableParams.backupUuid = UUID.randomUUID();
    tableParams.baseBackupUUID = tableParams.backupUuid;
    tableParams.backupParamsIdentifier = UUID.randomUUID();
    tableParams.setUniverseUUID(UUID.randomUUID());
    tableParams.setKeyspace("foo");
    String backupIdentifier = "univ-" + tableParams.getUniverseUUID().toString() + "/";
    SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");
    String backupLocationTS = tsFormat.format(new Date());
    BackupUtil.updateDefaultStorageLocation(
        tableParams,
        testCustomer.getUuid(),
        BackupCategory.YB_BACKUP_SCRIPT,
        BackupVersion.V2,
        backupLocationTS);
    String expectedStorageLocation = formData.get("data").get("BACKUP_LOCATION").asText();
    expectedStorageLocation =
        expectedStorageLocation.endsWith("/")
            ? (expectedStorageLocation + backupIdentifier)
            : (expectedStorageLocation + "/" + backupIdentifier);
    assertTrue(tableParams.storageLocation.contains(expectedStorageLocation));
    if (testConfig.getName().equals("NFS")) {
      // Assert no double slash occurance
      assertEquals(1, tableParams.storageLocation.split("//").length);
    } else {
      assertEquals(2, tableParams.storageLocation.split("//").length);
    }
  }

  @Test
  @Parameters(method = "updateStorageLocationParams")
  public void testUpdateDefaultStorageLocationWithYbc(JsonNode formData) {
    CustomerConfig testConfig = CustomerConfig.createWithFormData(testCustomer.getUuid(), formData);
    BackupTableParams tableParams = new BackupTableParams();
    tableParams.storageConfigUUID = testConfig.getConfigUUID();
    tableParams.backupUuid = UUID.randomUUID();
    tableParams.baseBackupUUID = tableParams.backupUuid;
    tableParams.backupParamsIdentifier = UUID.randomUUID();
    tableParams.setUniverseUUID(UUID.randomUUID());
    tableParams.setKeyspace("foo");
    String backupIdentifier = "univ-" + tableParams.getUniverseUUID().toString() + "/";
    SimpleDateFormat tsFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss");
    String backupLocationTS = tsFormat.format(new Date());
    BackupUtil.updateDefaultStorageLocation(
        tableParams,
        testCustomer.getUuid(),
        BackupCategory.YB_CONTROLLER,
        BackupVersion.V2,
        backupLocationTS);
    String expectedStorageLocation = formData.get("data").get("BACKUP_LOCATION").asText();
    if (testConfig.getName().equals("NFS")) {
      backupIdentifier = "yugabyte_backup/" + backupIdentifier;
      expectedStorageLocation =
          expectedStorageLocation.endsWith("/")
              ? (expectedStorageLocation + backupIdentifier)
              : (expectedStorageLocation + "/" + backupIdentifier);
      assertEquals(1, tableParams.storageLocation.split("//").length);
    } else {
      expectedStorageLocation =
          expectedStorageLocation.endsWith("/")
              ? (expectedStorageLocation + backupIdentifier)
              : (expectedStorageLocation + "/" + backupIdentifier);
      assertEquals(2, tableParams.storageLocation.split("//").length);
    }
    assertTrue(tableParams.storageLocation.contains(expectedStorageLocation));
  }
}
