// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.common.AWSUtil.AWS_ACCESS_KEY_ID_FIELDNAME;
import static com.yugabyte.yw.common.AWSUtil.AWS_SECRET_ACCESS_KEY_FIELDNAME;
import static com.yugabyte.yw.common.AZUtil.AZURE_STORAGE_SAS_TOKEN_FIELDNAME;
import static com.yugabyte.yw.common.GCPUtil.GCS_CREDENTIALS_JSON_FIELDNAME;
import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static com.yugabyte.yw.models.helpers.BaseBeanValidator.fieldFullName;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_AZURE;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_GCS;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_S3;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.REGION_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.REGION_LOCATIONS_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.REGION_LOCATION_FIELDNAME;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.configs.StubbedCustomerConfigValidator;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class CustomerConfigValidatorTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private CustomerConfigValidator customerConfigValidator;

  private List<String> allowedBuckets = new ArrayList<>();

  @Before
  public void setUp() {
    customerConfigValidator =
        new StubbedCustomerConfigValidator(
            app.injector().instanceOf(BeanValidator.class), allowedBuckets);
  }

  @Test
  // @formatter:off
  @Parameters({
    "NFS, BACKUP_LOCATION, /tmp, true",
    "NFS, BACKUP_LOCATION, tmp, false",
    "NFS, BACKUP_LOCATION, /mnt/storage, true",
    "NFS, BACKUP_LOCATION, //, true",
    "NFS, BACKUP_LOCATION, $(ping -c1 google.com.ru > /tmp/ping_log)/tmp/some/nfs/dir, false",
    "NFS, BACKUP_LOCATION,, false",
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com/test/itest, true",
    "S3, AWS_HOST_BASE, s3://backups.yugabyte.com/test/itest, false", // BACKUP_LOCATION undefined
    "S3, BACKUP_LOCATION, s3.amazonaws.com, false",
    "S3, BACKUP_LOCATION, ftp://s3.amazonaws.com, false",
    "S3, BACKUP_LOCATION,, false",
    "GCS, BACKUP_LOCATION, gs://itest-backup, true",
    "GCS, BACKUP_LOCATION, gs://itest-backup/test, true",
    "GCS, BACKUP_LOCATION, https://storage.googleapis.com/itest-backup/test, true",
    "GCS, BACKUP_LOCATION, gcp.test.com, false",
    "GCS, BACKUP_LOCATION, ftp://gcp.test.com, false",
    "GCS, BACKUP_LOCATION,, false",
    "AZ, BACKUP_LOCATION, https://www.microsoft.com/azure, true",
    "AZ, BACKUP_LOCATION, http://www.microsoft.com/azure, false",
    "AZ, BACKUP_LOCATION, www.microsoft.com/azure, false",
    "AZ, BACKUP_LOCATION, ftp://www.microsoft.com/azure, false",
    "AZ, BACKUP_LOCATION,, false",
  })
  // @formatter:on
  public void testValidateDataContent_Storage_OneParamToCheck(
      String storageType, String fieldName, String fieldValue, boolean expectedResult) {
    ObjectNode data = Json.newObject().put(fieldName, fieldValue);
    addRequiredCredentials(storageType, data);
    CustomerConfig config = createConfig(ConfigType.STORAGE, storageType, data);
    if (expectedResult) {
      if (fieldValue.length() > 5) {
        String strippedValue;
        if (fieldValue.startsWith("https://storage.googleapis.com/")) {
          strippedValue = fieldValue.substring(31);
        } else {
          strippedValue = fieldValue.substring(5);
        }
        allowedBuckets.add(strippedValue.split("/", 2)[0]);
      }
      customerConfigValidator.validateConfig(config);
    } else {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(PlatformServiceException.class));
    }
  }

  private void addRequiredCredentials(String storageType, ObjectNode data) {
    if (storageType.equals("AZ")) {
      data.put(AZURE_STORAGE_SAS_TOKEN_FIELDNAME, "XXXXX");
    } else if (storageType.equals("GCS")) {
      data.put(GCS_CREDENTIALS_JSON_FIELDNAME, "{}");
    } else if (storageType.equals("S3")) {
      data.put(AWS_ACCESS_KEY_ID_FIELDNAME, "XXXXX");
      data.put(AWS_SECRET_ACCESS_KEY_FIELDNAME, "XXXXX");
    }
  }

  @Test
  // @formatter:off
  @Parameters({
    // location - correct, aws_host_base - empty -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com/test/itest, AWS_HOST_BASE,, true",
    // location - correct, aws_host_base - incorrect -> disallowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com/test/itest, "
        + "AWS_HOST_BASE, ftp://s3.amazonaws.com, false",
    // location - correct, aws_host_base - correct -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com/test/itest, "
        + "AWS_HOST_BASE, s3.amazonaws.com, true",
    // location - correct, aws_host_base - correct -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, AWS_HOST_BASE, s3.amazonaws.com, true",
    // location - correct, aws_host_base(for S3 compatible storage) - incorrect -> disallowed
    "S3, BACKUP_LOCATION, s3://false, AWS_HOST_BASE, http://fake-localhost:9000, false",
    // location - correct, aws_host_base - correct -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, AWS_HOST_BASE, s3.amazonaws.com:443, true",
    // location - correct, aws_host_base - correct -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, AWS_HOST_BASE, minio.rmn.local:30000, true",
    // location - correct, aws_host_base - correct -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, AWS_HOST_BASE, https://s3.amazonaws.com, true",
    // location - correct, aws_host_base - correct -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, "
        + " AWS_HOST_BASE, https://s3.amazonaws.com:443, true",
    // location - correct, aws_host_base(negative port value) - incorrect -> disallowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, "
        + " AWS_HOST_BASE, http://s3.amazonaws.com:-443, false",
    // location - correct, aws_host_base(negative port value) - incorrect -> disallowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, AWS_HOST_BASE, s3.amazonaws.com:-443, false",
    // location - correct, aws_host_base - correct -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com/test/itest, "
        + "AWS_HOST_BASE, cloudstorage.onefs.dell.com, true",
    // location - incorrect, aws_host_base - correct -> disallowed
    "S3, BACKUP_LOCATION, ftp://backups.yugabyte.com/test/itest, "
        + "AWS_HOST_BASE, s3.amazonaws.com, false",
    // location - incorrect, aws_host_base - empty -> disallowed
    "S3, BACKUP_LOCATION, ftp://backups.yugabyte.com/test/itest, AWS_HOST_BASE,, false",
    // location - empty, aws_host_base - correct -> disallowed
    "S3, BACKUP_LOCATION,, AWS_HOST_BASE, s3.amazonaws.com, false",
    // location - empty, aws_host_base - empty -> disallowed
    "S3, BACKUP_LOCATION,, AWS_HOST_BASE,, false",
  })
  // @formatter:on
  public void testValidateDataContent_Storage_TwoParamsToCheck(
      String storageType,
      String fieldName1,
      String fieldValue1,
      String fieldName2,
      String fieldValue2,
      boolean expectedResult) {
    ObjectNode data = Json.newObject();
    data.put(fieldName1, fieldValue1);
    data.put(fieldName2, fieldValue2);
    addRequiredCredentials(storageType, data);
    CustomerConfig config = createConfig(ConfigType.STORAGE, storageType, data);
    if (expectedResult) {
      allowedBuckets.add(fieldValue1.substring(5).split("/", 2)[0]);
      customerConfigValidator.validateConfig(config);
    } else {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(PlatformServiceException.class));
    }
  }

  @Parameters({
    // Check invalid AWS Credentials -> disallowed.
    "s3://test, The AWS Access Key Id you provided does not exist in our records., true",
    // BACKUP_LOCATION - incorrect -> disallowed.
    "https://abc, Invalid s3UriPath format: https://abc, false",
    // Valid case.
    "s3://test, null, false",
  })
  @Test
  public void testValidateDataContent_Storage_S3PreflightCheckValidator(
      String backupLocation, @Nullable String expectedMessage, boolean refuseKeys) {
    ((StubbedCustomerConfigValidator) customerConfigValidator).setRefuseKeys(refuseKeys);
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, backupLocation);
    data.put(AWS_ACCESS_KEY_ID_FIELDNAME, "testAccessKey");
    data.put(AWS_SECRET_ACCESS_KEY_FIELDNAME, "SecretKey");
    CustomerConfig config = createConfig(ConfigType.STORAGE, NAME_S3, data);
    if ((expectedMessage != null) && !expectedMessage.equals("")) {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(
              PlatformServiceException.class,
              "errorJson: {\""
                  + fieldFullName(BACKUP_LOCATION_FIELDNAME)
                  + "\":[\""
                  + expectedMessage
                  + "\"]}"));
    } else {
      allowedBuckets.add(backupLocation.substring(5).split("/", 2)[0]);
      customerConfigValidator.validateConfig(config);
    }
  }

  @Parameters({
    // Valid case with location URL equal to backup URL.
    "s3://test, null",
    // Valid case with location URL different from backup URL.
    "s3://test2, null",
    // Invalid location URL (wrong format).
    "ftp://test2, Invalid field value 'ftp://test2'",
    // Valid bucket.
    "s3://test2, S3 URI path s3://test2 doesn't exist",
  })
  @Test
  public void testValidateDataContent_Storage_S3PreflightCheckValidator_RegionLocation(
      String regionLocation, @Nullable String expectedMessage) {
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, "s3://test");
    allowedBuckets.add("test");
    data.put(AWS_ACCESS_KEY_ID_FIELDNAME, "testAccessKey");
    data.put(AWS_SECRET_ACCESS_KEY_FIELDNAME, "SecretKey");

    ObjectMapper mapper = new ObjectMapper();
    ArrayNode regionLocations = mapper.createArrayNode();

    ObjectNode regionData = Json.newObject();
    regionData.put(CustomerConfigConsts.REGION_FIELDNAME, "eu");
    regionData.put(REGION_LOCATION_FIELDNAME, regionLocation);
    regionLocations.add(regionData);
    data.put(REGION_LOCATIONS_FIELDNAME, regionLocations);

    CustomerConfig config = createConfig(ConfigType.STORAGE, NAME_S3, data);
    if ((expectedMessage != null) && !expectedMessage.equals("")) {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(
              PlatformServiceException.class,
              "errorJson: {\""
                  + fieldFullName(REGION_LOCATION_FIELDNAME)
                  + "\":[\""
                  + expectedMessage
                  + "\"]}"));
    } else {
      allowedBuckets.add(regionLocation.substring(5).split("/", 2)[0]);
      customerConfigValidator.validateConfig(config);
    }
  }

  @Parameters({
    // BACKUP_LOCATION - incorrect -> disallowed.
    "https://abc, {}, GS Uri path https://abc doesn't exist, false",
    // Check empty GCP Credentials Json -> disallowed.
    "gs://test, {}, Invalid GCP Credential Json., true",
    // Valid case.
    "gs://test, {}, null, false",
    // Valid case.
    "https://storage.googleapis.com/, {}, null, false"
  })
  @Test
  public void testValidateDataContent_Storage_GCSPreflightCheckValidator(
      String backupLocation,
      String credentialsJson,
      @Nullable String expectedMessage,
      boolean refuseCredentials) {
    ((StubbedCustomerConfigValidator) customerConfigValidator).setRefuseKeys(refuseCredentials);
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, backupLocation);
    data.put(GCS_CREDENTIALS_JSON_FIELDNAME, credentialsJson);
    CustomerConfig config = createConfig(ConfigType.STORAGE, NAME_GCS, data);
    if ((expectedMessage != null) && !expectedMessage.equals("")) {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(
              PlatformServiceException.class,
              "errorJson: {\""
                  + fieldFullName(BACKUP_LOCATION_FIELDNAME)
                  + "\":[\""
                  + expectedMessage
                  + "\"]}"));
    } else {
      allowedBuckets.add(backupLocation);
      customerConfigValidator.validateConfig(config);
    }
  }

  @Parameters({

    // Valid case.
    "gs://test, null",
    // Valid case.
    "gs://test2, null",
  })
  @Test
  public void testValidateDataContent_Storage_GCSPreflightCheckValidator_RegionLocation(
      String regionLocation, @Nullable String expectedMessage) {
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, "gs://test");
    allowedBuckets.add("test");
    data.put(GCS_CREDENTIALS_JSON_FIELDNAME, "{}");

    ObjectMapper mapper = new ObjectMapper();
    ArrayNode regionLocations = mapper.createArrayNode();

    ObjectNode regionData = Json.newObject();
    regionData.put(REGION_FIELDNAME, "eu");
    regionData.put(REGION_LOCATION_FIELDNAME, regionLocation);
    regionLocations.add(regionData);
    data.put(REGION_LOCATIONS_FIELDNAME, regionLocations);

    CustomerConfig config = createConfig(ConfigType.STORAGE, NAME_GCS, data);
    if ((expectedMessage != null) && !expectedMessage.equals("")) {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(
              PlatformServiceException.class,
              "errorJson: {\""
                  + fieldFullName(REGION_LOCATION_FIELDNAME)
                  + "\":[\""
                  + expectedMessage
                  + "\"]}"));
    } else {
      allowedBuckets.add(regionLocation);
      customerConfigValidator.validateConfig(config);
    }
  }

  @Parameters({
    "https://stoe.ws.net/container, fakeToken, null, false, true",
    "https://stoe.ws.net, fakeToken, Invalid azUriPath format: https://stoe.ws.net, false, true",
    "http://stoe.ws.net, fakeToken, Invalid field value 'http://stoe.ws.net', false, true",
    "https://storagetestazure.windows.net/container, tttt, Invalid SAS token!, true, true",
    "https://stoe.ws.net/container1, tttt, Blob container container1 doesn't exist, false, false",
  })
  @Test
  public void testValidateDataContent_Storage_AZPreflightCheckValidator(
      String containerUrl,
      String sasToken,
      @Nullable String expectedMessage,
      boolean refuseCredentials,
      boolean isContainerExist) {
    ((StubbedCustomerConfigValidator) customerConfigValidator).setRefuseKeys(refuseCredentials);
    when(((StubbedCustomerConfigValidator) customerConfigValidator).blobContainerClient.exists())
        .thenReturn(isContainerExist);
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, containerUrl);
    data.put(AZUtil.AZURE_STORAGE_SAS_TOKEN_FIELDNAME, sasToken);
    CustomerConfig config = createConfig(ConfigType.STORAGE, NAME_AZURE, data);
    if ((expectedMessage != null) && !expectedMessage.equals("")) {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(
              PlatformServiceException.class,
              "errorJson: {\""
                  + fieldFullName(BACKUP_LOCATION_FIELDNAME)
                  + "\":[\""
                  + expectedMessage
                  + "\"]}"));
    } else {
      customerConfigValidator.validateConfig(config);
    }
  }

  private CustomerConfig createConfig(ConfigType type, String name, ObjectNode data) {
    return new CustomerConfig()
        .setCustomerUUID(UUID.randomUUID())
        .setName(name)
        .setConfigName(name)
        .setType(type)
        .setData(data);
  }
}
