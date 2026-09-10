// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.validators;

import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_OCI;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.OCI_NAMESPACE_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.OCI_REGION_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.OCI_S3_ACCESS_KEY_ID_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.OCI_S3_HOST_BASE_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.OCI_S3_SECRET_ACCESS_KEY_FIELDNAME;
import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.USE_OCI_IAM_FIELDNAME;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.nullable;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.configs.StubbedCustomerConfigValidator;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class CustomerConfigStorageOCIValidatorTest extends FakeDBApplication {

  private CustomerConfigValidator customerConfigValidator;
  private final List<String> allowedBuckets = new ArrayList<>();

  @Before
  public void setUp() {
    customerConfigValidator =
        new StubbedCustomerConfigValidator(
            app.injector().instanceOf(BeanValidator.class),
            allowedBuckets,
            mockStorageUtilFactory,
            app.injector().instanceOf(RuntimeConfGetter.class),
            mockAWSUtil,
            mockAZUtil,
            mockGCPUtil,
            mockOCIUtil);
    doCallRealMethod().when(mockAWSUtil).getRegionLocationsMap(any());
    doCallRealMethod()
        .when(mockAWSUtil)
        .getCloudLocationInfo(nullable(String.class), any(), nullable(String.class));
    doNothing().when(mockOCIUtil).validateIamConfig(any());
  }

  private CustomerConfig createConfig(ObjectNode data) {
    return new CustomerConfig()
        .setCustomerUUID(UUID.randomUUID())
        .setName(NAME_OCI)
        .setConfigName("test-oci-config")
        .setType(ConfigType.STORAGE)
        .setData(data);
  }

  private ObjectNode iamData() {
    ObjectNode data = Json.newObject();
    data.put(
        BACKUP_LOCATION_FIELDNAME,
        "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket");
    data.put(OCI_REGION_FIELDNAME, "us-sanjose-1");
    data.put(OCI_NAMESPACE_FIELDNAME, "test-namespace");
    data.put(USE_OCI_IAM_FIELDNAME, true);
    return data;
  }

  @Test
  public void testValidateS3CompatibleSuccess() {
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, "s3://test-bucket/prefix");
    data.put(OCI_S3_ACCESS_KEY_ID_FIELDNAME, "access-key");
    data.put(OCI_S3_SECRET_ACCESS_KEY_FIELDNAME, "secret-key");
    data.put(OCI_REGION_FIELDNAME, "us-sanjose-1");
    data.put(
        OCI_S3_HOST_BASE_FIELDNAME,
        "test-namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com");
    data.put(USE_OCI_IAM_FIELDNAME, false);
    allowedBuckets.add("test-bucket");
    customerConfigValidator.validateConfig(createConfig(data));
  }

  @Test
  public void testValidateS3CompatibleWithNativeLocationSuccess() {
    ObjectNode data = Json.newObject();
    data.put(
        BACKUP_LOCATION_FIELDNAME,
        "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket");
    data.put(OCI_S3_ACCESS_KEY_ID_FIELDNAME, "access-key");
    data.put(OCI_S3_SECRET_ACCESS_KEY_FIELDNAME, "secret-key");
    data.put(OCI_REGION_FIELDNAME, "us-sanjose-1");
    data.put(OCI_NAMESPACE_FIELDNAME, "test-namespace");
    data.put(
        OCI_S3_HOST_BASE_FIELDNAME,
        "test-namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com");
    data.put(USE_OCI_IAM_FIELDNAME, false);
    allowedBuckets.add("test-bucket");
    customerConfigValidator.validateConfig(createConfig(data));
  }

  @Test
  public void testValidateIamSuccess() {
    customerConfigValidator.validateConfig(createConfig(iamData()));
  }

  @Test
  public void testValidateIamWithS3StyleLocationSuccess() {
    ObjectNode data = iamData();
    data.put(BACKUP_LOCATION_FIELDNAME, "s3://test-bucket");
    customerConfigValidator.validateConfig(createConfig(data));
  }

  @Test
  public void testValidateRejectsBothIamAndStaticCreds() {
    ObjectNode data = iamData();
    data.put(OCI_S3_ACCESS_KEY_ID_FIELDNAME, "access-key");
    data.put(OCI_S3_SECRET_ACCESS_KEY_FIELDNAME, "secret-key");
    data.put(
        OCI_S3_HOST_BASE_FIELDNAME,
        "test-namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com");
    assertThat(
        () -> customerConfigValidator.validateConfig(createConfig(data)),
        thrown(PlatformServiceException.class));
  }

  @Test
  public void testValidateRejectsNeitherIamNorStaticCreds() {
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, "s3://test-bucket/prefix");
    data.put(OCI_REGION_FIELDNAME, "us-sanjose-1");
    data.put(USE_OCI_IAM_FIELDNAME, false);
    assertThat(
        () -> customerConfigValidator.validateConfig(createConfig(data)),
        thrown(PlatformServiceException.class));
  }

  @Test
  public void testValidateIamRequiresNamespace() {
    ObjectNode data = iamData();
    data.remove(OCI_NAMESPACE_FIELDNAME);
    assertThat(
        () -> customerConfigValidator.validateConfig(createConfig(data)),
        thrown(PlatformServiceException.class));
  }

  @Test
  public void testValidateIamWithNativeUrlPrefixSuccess() {
    ObjectNode data = iamData();
    data.put(
        BACKUP_LOCATION_FIELDNAME,
        "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket/prefix");
    customerConfigValidator.validateConfig(createConfig(data));
  }

  @Test
  public void testValidateIamRejectsLegacyObjectPath() {
    ObjectNode data = iamData();
    data.put(
        BACKUP_LOCATION_FIELDNAME,
        "https://objectstorage.us-sanjose-1.oraclecloud.com/n/test-namespace/b/test-bucket/o/prefix");
    assertThat(
        () -> customerConfigValidator.validateConfig(createConfig(data)),
        thrown(PlatformServiceException.class));
  }

  @Test
  public void testValidateIamRejectsNamespaceMismatch() {
    ObjectNode data = iamData();
    data.put(OCI_NAMESPACE_FIELDNAME, "other-namespace");
    try {
      customerConfigValidator.validateConfig(createConfig(data));
      throw new AssertionError("expected PlatformServiceException");
    } catch (PlatformServiceException e) {
      String message = e.getMessage();
      assertTrue(
          "namespace mismatch should be attributed to OCI_NAMESPACE, got: " + message,
          message.contains("data.OCI_NAMESPACE"));
      assertFalse(
          "namespace mismatch must not be masked as a location error, got: " + message,
          message.contains("data.BACKUP_LOCATION"));
    }
  }

  @Test
  public void testValidateS3CompatibleRequiresHostBase() {
    ObjectNode data = Json.newObject();
    data.put(BACKUP_LOCATION_FIELDNAME, "s3://test-bucket/prefix");
    data.put(OCI_S3_ACCESS_KEY_ID_FIELDNAME, "access-key");
    data.put(OCI_S3_SECRET_ACCESS_KEY_FIELDNAME, "secret-key");
    data.put(OCI_REGION_FIELDNAME, "us-sanjose-1");
    data.put(USE_OCI_IAM_FIELDNAME, false);
    assertThat(
        () -> customerConfigValidator.validateConfig(createConfig(data)),
        thrown(PlatformServiceException.class));
  }
}
