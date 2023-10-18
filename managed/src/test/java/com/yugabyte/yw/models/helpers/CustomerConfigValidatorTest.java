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
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyString;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.amazonaws.services.s3.model.ObjectListing;
import com.amazonaws.services.s3.model.S3Object;
import com.amazonaws.services.s3.model.S3ObjectInputStream;
import com.amazonaws.services.s3.model.S3ObjectSummary;
import com.azure.core.http.rest.PagedIterable;
import com.azure.storage.blob.BlobClient;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.models.BlobItem;
import com.azure.storage.blob.models.BlobStorageException;
import com.azure.storage.blob.specialized.BlobInputStream;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.CloudUtil;
import com.yugabyte.yw.common.CloudUtil.ExtraPermissionToValidate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.configs.StubbedCustomerConfigValidator;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.stream.Stream;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.invocation.InvocationOnMock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.mockito.stubbing.Answer;
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
            app.injector().instanceOf(BeanValidator.class),
            allowedBuckets,
            mockStorageUtilFactory,
            app.injector().instanceOf(RuntimeConfGetter.class),
            mockAWSUtil);
    when(mockStorageUtilFactory.getCloudUtil("AZ")).thenReturn(mockAZUtil);
    doCallRealMethod().when(mockAWSUtil).getConfigLocationInfo(any());
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
    // location - correct, aws_host_base - incorrect(443 port not allowed) -> disallowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, AWS_HOST_BASE, s3.amazonaws.com:443, false",
    // location - correct, aws_host_base - correct -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, AWS_HOST_BASE, minio.rmn.local:30000, true",
    // location - correct, aws_host_base - correct -> allowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, AWS_HOST_BASE, https://s3.amazonaws.com, true",
    // location - correct, aws_host_base - incorrect(443 port not allowed) -> disallowed
    "S3, BACKUP_LOCATION, s3://backups.yugabyte.com, "
        + " AWS_HOST_BASE, https://s3.amazonaws.com:443, false",
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

  private CustomerConfig createS3Config(String bucketName) {
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, "s3://" + bucketName);
    data.put(AWS_ACCESS_KEY_ID_FIELDNAME, "testAccessKey");
    data.put(AWS_SECRET_ACCESS_KEY_FIELDNAME, "SecretKey");
    return createConfig(ConfigType.STORAGE, NAME_S3, data);
  }

  @Test
  public void testValidateDataContent_Storage_S3PreflightCheckValidatorCRUDUnsupportedPermission() {
    String bucket = "test";
    AmazonS3 client = mock(AmazonS3.class);
    doCallRealMethod()
        .when(mockAWSUtil)
        .validateOnBucket(client, bucket, "", ImmutableList.of(ExtraPermissionToValidate.NULL));
    assertThat(
        () ->
            mockAWSUtil.validateOnBucket(
                client, bucket, "", ImmutableList.of(ExtraPermissionToValidate.NULL)),
        thrown(
            PlatformServiceException.class,
            "Unsupported permission "
                + ExtraPermissionToValidate.NULL.toString()
                + " validation is not supported!"));
  }

  @Test
  public void testValidateDataContent_Stroage_S3PreflightCheckValidator_BucketCrudSuccess()
      throws IOException {
    String bucketName = "test";
    CustomerConfig config = createS3Config(bucketName);
    AmazonS3 client = ((StubbedCustomerConfigValidator) customerConfigValidator).s3Client;

    doCallRealMethod()
        .when(mockAWSUtil)
        .validateOnBucket(
            client,
            bucketName,
            "",
            ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST));

    UUID myUUID = UUID.randomUUID();
    String fileName = myUUID.toString() + ".txt";
    when(mockAWSUtil.getRandomUUID()).thenReturn(myUUID);

    S3Object object = mock(S3Object.class);
    S3ObjectInputStream inputStream = mock(S3ObjectInputStream.class);

    String incorrectData = "notdummy";
    when(client.getObject(anyString(), anyString())).thenReturn(object);
    when(object.getObjectContent()).thenReturn(inputStream);

    doAnswer(
            new Answer() {
              @Override
              public Object answer(InvocationOnMock invocation) {
                Object[] args = invocation.getArguments();
                byte[] data = (byte[]) args[0];
                byte[] content = CloudUtil.DUMMY_DATA.getBytes();
                System.arraycopy(content, 0, data, 0, content.length);
                return null;
              }
            })
        .when(inputStream)
        .read(any());

    List<S3ObjectSummary> objSummaryList = new ArrayList<>();
    ObjectListing mockObjectListing = mock(ObjectListing.class);
    when(mockObjectListing.getObjectSummaries()).thenReturn(objSummaryList);
    when(client.listObjects(bucketName, "")).thenReturn(mockObjectListing);

    S3ObjectSummary objSummary = mock(S3ObjectSummary.class);
    when(objSummary.getKey()).thenReturn(fileName);

    objSummaryList.add(objSummary);
    when(client.doesObjectExist(bucketName, fileName)).thenReturn(false);

    customerConfigValidator.validateConfig(config);
  }

  @Test
  public void testValidateDataContent_Stroage_S3PreflightCheckValidator_BucketCrudFailCreate() {
    String bucketName = "test";
    CustomerConfig config = createS3Config(bucketName);
    AmazonS3 client = ((StubbedCustomerConfigValidator) customerConfigValidator).s3Client;

    doCallRealMethod()
        .when(mockAWSUtil)
        .validateOnBucket(
            client,
            bucketName,
            "",
            ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST));

    UUID myUUID = UUID.randomUUID();
    String fileName = myUUID.toString() + ".txt";
    when(mockAWSUtil.getRandomUUID()).thenReturn(myUUID);

    doThrow(new AmazonS3Exception("Put object failed"))
        .when(client)
        .putObject(anyString(), anyString(), anyString());

    assertThat(
        () -> customerConfigValidator.validateConfig(config),
        thrown(PlatformServiceException.class));
  }

  @Test
  public void testValidateDataContent_Stroage_S3PreflightCheckValidator_BucketCrudFailRead()
      throws IOException {
    String bucketName = "test";
    CustomerConfig config = createS3Config(bucketName);
    AmazonS3 client = ((StubbedCustomerConfigValidator) customerConfigValidator).s3Client;

    UUID myUUID = UUID.randomUUID();
    String fileName = myUUID.toString() + ".txt";
    when(mockAWSUtil.getRandomUUID()).thenReturn(myUUID);

    doCallRealMethod()
        .when(mockAWSUtil)
        .validateOnBucket(
            client,
            bucketName,
            "",
            ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST));

    S3Object object = mock(S3Object.class);
    S3ObjectInputStream inputStream = mock(S3ObjectInputStream.class);

    String incorrectData = "notdummy";

    when(client.getObject(anyString(), anyString())).thenReturn(object);
    when(object.getObjectContent()).thenReturn(inputStream);

    doAnswer(
            new Answer() {
              @Override
              public Object answer(InvocationOnMock invocation) {
                Object[] args = invocation.getArguments();
                byte[] data = (byte[]) args[0];
                byte[] content = incorrectData.getBytes();
                System.arraycopy(content, 0, data, 0, content.length);
                return null;
              }
            })
        .when(inputStream)
        .read(any());

    byte[] tmp = new byte[CloudUtil.DUMMY_DATA.getBytes().length];
    byte[] content = incorrectData.getBytes();
    System.arraycopy(content, 0, tmp, 0, content.length);
    assertThat(
        () -> customerConfigValidator.validateConfig(config),
        thrown(
            PlatformServiceException.class,
            "Error reading test object "
                + fileName
                + ", expected: \""
                + CloudUtil.DUMMY_DATA
                + "\", got: \""
                + new String(tmp)
                + "\""));
  }

  @Test
  public void testValidateDataContent_Stroage_S3PreflightCheckValidator_BucketCrudFailList()
      throws IOException {
    String bucketName = "test";
    CustomerConfig config = createS3Config(bucketName);
    AmazonS3 client = ((StubbedCustomerConfigValidator) customerConfigValidator).s3Client;

    UUID myUUID = UUID.randomUUID();
    String fileName = myUUID.toString() + ".txt";
    when(mockAWSUtil.getRandomUUID()).thenReturn(myUUID);

    doCallRealMethod()
        .when(mockAWSUtil)
        .validateOnBucket(
            client,
            bucketName,
            "",
            ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST));

    S3Object object = mock(S3Object.class);
    S3ObjectInputStream inputStream = mock(S3ObjectInputStream.class);

    String incorrectData = "notdummy";

    when(client.getObject(anyString(), anyString())).thenReturn(object);
    when(object.getObjectContent()).thenReturn(inputStream);

    doAnswer(
            new Answer() {
              @Override
              public Object answer(InvocationOnMock invocation) {
                Object[] args = invocation.getArguments();
                byte[] data = (byte[]) args[0];
                byte[] content = CloudUtil.DUMMY_DATA.getBytes();
                System.arraycopy(content, 0, data, 0, content.length);
                return null;
              }
            })
        .when(inputStream)
        .read(any());

    List<S3ObjectSummary> objSummaryList = new ArrayList<>();
    ObjectListing mockObjectListing = mock(ObjectListing.class);
    when(mockObjectListing.getObjectSummaries()).thenReturn(objSummaryList);
    when(client.listObjects(bucketName, "")).thenReturn(mockObjectListing);

    assertThat(
        () -> customerConfigValidator.validateConfig(config),
        thrown(
            PlatformServiceException.class,
            "Test object " + fileName + " was not found in bucket test objects list."));
  }

  @Test
  public void testValidateDataContent_Stroage_S3PreflightCheckValidator_BucketCrudFailDelete()
      throws IOException {
    String bucketName = "test";
    CustomerConfig config = createS3Config(bucketName);
    AmazonS3 client = ((StubbedCustomerConfigValidator) customerConfigValidator).s3Client;

    UUID myUUID = UUID.randomUUID();
    String fileName = myUUID.toString() + ".txt";
    when(mockAWSUtil.getRandomUUID()).thenReturn(myUUID);

    doCallRealMethod()
        .when(mockAWSUtil)
        .validateOnBucket(
            client,
            bucketName,
            "",
            ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST));

    S3Object object = mock(S3Object.class);
    S3ObjectInputStream inputStream = mock(S3ObjectInputStream.class);

    String incorrectData = "notdummy";

    when(client.getObject(anyString(), anyString())).thenReturn(object);
    when(object.getObjectContent()).thenReturn(inputStream);

    doAnswer(
            new Answer() {
              @Override
              public Object answer(InvocationOnMock invocation) {
                Object[] args = invocation.getArguments();
                byte[] data = (byte[]) args[0];
                byte[] content = CloudUtil.DUMMY_DATA.getBytes();
                System.arraycopy(content, 0, data, 0, content.length);
                return null;
              }
            })
        .when(inputStream)
        .read(any());

    List<S3ObjectSummary> objSummaryList = new ArrayList<>();
    ObjectListing mockObjectListing = mock(ObjectListing.class);
    when(mockObjectListing.getObjectSummaries()).thenReturn(objSummaryList);
    when(client.listObjects(bucketName, "")).thenReturn(mockObjectListing);

    S3ObjectSummary objSummary = mock(S3ObjectSummary.class);
    when(objSummary.getKey()).thenReturn(fileName);
    objSummaryList.add(objSummary);
    when(client.doesObjectExist(bucketName, fileName)).thenReturn(true);

    assertThat(
        () -> customerConfigValidator.validateConfig(config),
        thrown(
            PlatformServiceException.class,
            "Test object " + fileName + " was found in bucket " + bucketName));
  }

  @Parameters({
    // Valid case with location URL equal to backup URL.
    "s3://test, null",
    // Valid case with location URL different from backup URL.
    "s3://test2, null",
    // Invalid location URL (wrong format).
    "ftp://test2, Invalid field value 'ftp://test2'"
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

  private CustomerConfig createAzureConfig() {
    String containerUrl = "https://storagetestazure.windows.net/container";
    String sasToken = "fakeToken";
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, containerUrl);
    data.put(AZUtil.AZURE_STORAGE_SAS_TOKEN_FIELDNAME, sasToken);
    CustomerConfig config = createConfig(ConfigType.STORAGE, NAME_AZURE, data);
    return config;
  }

  private void setupAzureReadValidation(
      BlobContainerClient blobContainerClient,
      BlobClient blobClient,
      BlobInputStream blobIs,
      boolean shouldReadValidateFail,
      String incorrectData)
      throws IOException {
    when(blobContainerClient.getBlobClient(any())).thenReturn(blobClient);
    doCallRealMethod()
        .when(mockAZUtil)
        .validateOnBlobContainerClient(
            blobContainerClient,
            ImmutableList.of(ExtraPermissionToValidate.READ, ExtraPermissionToValidate.LIST));

    when(blobClient.openInputStream()).thenReturn(blobIs);

    doAnswer(
            new Answer() {
              @Override
              public Object answer(InvocationOnMock invocation) {
                Object[] args = invocation.getArguments();
                byte[] data = (byte[]) args[0];
                byte[] content =
                    shouldReadValidateFail
                        ? incorrectData.getBytes()
                        : CloudUtil.DUMMY_DATA.getBytes();
                System.arraycopy(content, 0, data, 0, content.length);
                return null;
              }
            })
        .when(blobIs)
        .read(any());
  }

  private Stream genListStream(BlobItem mockBlobItem) {
    List<BlobItem> list = new ArrayList<>();
    list.add(mockBlobItem);
    return list.stream();
  }

  private void setupAzureListAndDeleteValidation(
      BlobContainerClient blobContainerClient,
      BlobClient blobClient,
      boolean shouldListValidateFail,
      boolean shouldDeleteValidateFail,
      String fileName) {
    PagedIterable<BlobItem> mockIterable = mock(PagedIterable.class);
    when(blobContainerClient.listBlobs()).thenReturn(mockIterable);
    ArrayList<BlobItem> listBlobItems = new ArrayList<BlobItem>();
    BlobItem mockBlobItem = mock(BlobItem.class);
    when(mockBlobItem.getName()).thenReturn(fileName);
    when(mockIterable.spliterator()).thenAnswer(invocation -> listBlobItems.spliterator());

    if (!shouldListValidateFail) {
      listBlobItems.add(mockBlobItem);
      when(blobClient.exists()).thenReturn(shouldDeleteValidateFail);
    }
  }

  private void assertAzureReadFailure(
      String incorrectData, String fileName, CustomerConfig config) {
    byte[] tmp = new byte[CloudUtil.DUMMY_DATA.getBytes().length];
    byte[] content = incorrectData.getBytes();
    System.arraycopy(content, 0, tmp, 0, content.length);
    assertThat(
        () -> customerConfigValidator.validateConfig(config),
        thrown(
            PlatformServiceException.class,
            "Error reading test blob "
                + fileName
                + ", expected: \""
                + CloudUtil.DUMMY_DATA
                + "\", got: \""
                + new String(tmp)
                + "\""));
  }

  public void testValidateDataContent_Storage_AZPreflightCheckValidatorCRUDUnsupportedPermission() {
    BlobContainerClient bcc = mock(BlobContainerClient.class);
    doCallRealMethod()
        .when(mockAZUtil)
        .validateOnBlobContainerClient(bcc, ImmutableList.of(ExtraPermissionToValidate.NULL));
    assertThat(
        () ->
            mockAZUtil.validateOnBlobContainerClient(
                bcc, ImmutableList.of(ExtraPermissionToValidate.NULL)),
        thrown(
            PlatformServiceException.class,
            "Unsupported permission "
                + ExtraPermissionToValidate.NULL.toString()
                + " validation is not supported!"));
  }

  @Parameters({
    "0", "1", "2", "3", "4",
  })
  @Test
  public void testValidateDataContent_Storage_AZPreflightCheckValidatorCRUD(
      int validationStepToFail) throws IOException {
    boolean shouldCreateValidateFail = validationStepToFail >= 1;
    boolean shouldReadValidateFail = validationStepToFail >= 2;
    boolean shouldListValidateFail = validationStepToFail >= 3;
    boolean shouldDeleteValidateFail = validationStepToFail >= 4;

    UUID myUUID = UUID.randomUUID();
    String fileName = myUUID.toString() + ".txt";
    when(mockAZUtil.getRandomUUID()).thenReturn(myUUID);

    CustomerConfig config = createAzureConfig();
    BlobClient blobClient = mock(BlobClient.class);
    BlobInputStream blobIs = mock(BlobInputStream.class);
    BlobContainerClient blobContainerClient =
        ((StubbedCustomerConfigValidator) customerConfigValidator).blobContainerClient;

    if (shouldCreateValidateFail) {
      doThrow(new BlobStorageException("Upload failed", null, null)).when(blobClient).upload(any());
    }

    String incorrectData = "notdummy";
    setupAzureReadValidation(
        blobContainerClient, blobClient, blobIs, shouldReadValidateFail, incorrectData);

    setupAzureListAndDeleteValidation(
        blobContainerClient,
        blobClient,
        shouldListValidateFail,
        shouldDeleteValidateFail,
        fileName);

    if (shouldCreateValidateFail) {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(PlatformServiceException.class));
    } else if (shouldReadValidateFail) {
      assertAzureReadFailure(incorrectData, fileName, config);
    } else if (shouldListValidateFail) {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(
              PlatformServiceException.class,
              "Created blob with name \"" + fileName + "\" not found in list."));
    } else if (shouldDeleteValidateFail) {
      assertThat(
          () -> customerConfigValidator.validateConfig(config),
          thrown(
              PlatformServiceException.class,
              "Deleted blob \"" + fileName + "\" is still in the container"));
    } else {
      customerConfigValidator.validateConfig(config);
    }
  }

  @Parameters({
    "https://stoe.ws.net/container, fakeToken, null, false",
    "https://stoe.ws.net, fakeToken, Invalid azUriPath format: https://stoe.ws.net, false",
    "http://stoe.ws.net, fakeToken, Invalid field value 'http://stoe.ws.net', false",
    "https://storagetestazure.windows.net/container, tttt, Invalid SAS token!, true"
  })
  @Test
  public void testValidateDataContent_Storage_AZPreflightCheckValidator(
      String containerUrl,
      String sasToken,
      @Nullable String expectedMessage,
      boolean refuseCredentials)
      throws IOException {
    ((StubbedCustomerConfigValidator) customerConfigValidator).setRefuseKeys(refuseCredentials);

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
