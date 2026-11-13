// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.customer.config.UnsavedStorageConfigs;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import software.amazon.awssdk.awscore.exception.AwsErrorDetails;
import software.amazon.awssdk.services.s3.model.S3Exception;

public class ValidateStorageConfigTest extends FakeDBApplication {

  private Customer customer;
  private UnsavedStorageConfigs unsavedConfigs;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    unsavedConfigs = app.injector().instanceOf(UnsavedStorageConfigs.class);
  }

  /** A task with the real storage validators (NFS ones check the path without any storage). */
  private ValidateStorageConfig task(ValidateStorageConfig.Params params) {
    return task(app.injector().instanceOf(CustomerConfigValidator.class), params);
  }

  private ValidateStorageConfig task(
      CustomerConfigValidator validator, ValidateStorageConfig.Params params) {
    ValidateStorageConfig task =
        new ValidateStorageConfig(
            app.injector().instanceOf(BaseTaskDependencies.class), validator, unsavedConfigs);
    task.initialize(params);
    return task;
  }

  private static ValidateStorageConfig.Params params(UUID customerUUID, UUID configUUID) {
    ValidateStorageConfig.Params params = new ValidateStorageConfig.Params();
    params.customerUUID = customerUUID;
    params.configUUID = configUUID;
    return params;
  }

  /** A config saved by a v2 create, waiting for its check. */
  private CustomerConfig validatingNfs(String name, String location) {
    CustomerConfig config = ModelFactory.createNfsStorageConfig(customer, name, location);
    config.setState(ConfigState.Validating);
    config.update();
    return config;
  }

  private static CustomerConfigValidator failingWith(RuntimeException failure) {
    CustomerConfigValidator validator = mock(CustomerConfigValidator.class);
    doThrow(failure).when(validator).validateConfigDataWithValidator(any());
    return validator;
  }

  private static S3Exception s3Error(int status, String code, String message) {
    return (S3Exception)
        S3Exception.builder()
            .statusCode(status)
            .awsErrorDetails(AwsErrorDetails.builder().errorCode(code).build())
            .message(message)
            .build();
  }

  /** A v1 validator error for BACKUP_LOCATION, as the storage validators throw it. */
  private static PlatformServiceException v1FieldError(Throwable cause) {
    PlatformServiceException e =
        new PlatformServiceException(
            400, Json.toJson(ImmutableMap.of("data.BACKUP_LOCATION", List.of("check failed"))));
    e.initCause(cause);
    return e;
  }

  @Test
  public void passingCheckMakesTheConfigActive() {
    CustomerConfig config = validatingNfs("nfs", "/mnt/nfs");
    task(params(customer.getUuid(), config.getConfigUUID())).run();
    config.refresh();
    assertEquals(ConfigState.Active, config.getState());
  }

  @Test
  public void failingCheckDeletesTheConfig() {
    CustomerConfig config = validatingNfs("nfs", "relative/path");
    ValidateStorageConfig task = task(params(customer.getUuid(), config.getConfigUUID()));

    TaskExecutionException e = assertThrows(TaskExecutionException.class, task::run);
    assertEquals(Code.INTERNAL_ERROR, e.getCode());
    // Found by the storage validator without reaching the storage.
    assertTrue(
        e.getMessage(),
        e.getMessage().startsWith("STORAGE_CONFIG_INVALID: storage.backup_location"));
    assertNull(CustomerConfig.get(customer.getUuid(), config.getConfigUUID()));
    // The name is free again.
    assertNull(CustomerConfig.get(customer.getUuid(), "nfs"));
  }

  @Test
  public void providerRefusal() {
    CustomerConfig config = validatingNfs("s3ish", "/mnt");
    ValidateStorageConfig task =
        task(
            failingWith(v1FieldError(s3Error(403, "AccessDenied", "Access Denied"))),
            params(customer.getUuid(), config.getConfigUUID()));

    TaskExecutionException e = assertThrows(TaskExecutionException.class, task::run);
    assertEquals(Code.INTERNAL_ERROR, e.getCode());
    assertEquals(
        "STORAGE_VALIDATION_FAILED: storage.backup_location: check failed (provider status 403,"
            + " provider error code AccessDenied)",
        e.getMessage());
    assertNull(CustomerConfig.get(customer.getUuid(), config.getConfigUUID()));
  }

  @Test
  public void providerUnavailable() {
    CustomerConfig config = validatingNfs("slow", "/mnt");
    ValidateStorageConfig task =
        task(
            failingWith(s3Error(503, "SlowDown", "Slow Down")),
            params(customer.getUuid(), config.getConfigUUID()));

    TaskExecutionException e = assertThrows(TaskExecutionException.class, task::run);
    assertEquals(Code.CONNECTION_ERROR, e.getCode());
    assertTrue(
        e.getMessage(),
        e.getMessage().startsWith("STORAGE_PROVIDER_UNAVAILABLE: storage: Slow Down"));
    assertTrue(
        e.getMessage(),
        e.getMessage().endsWith("(provider status 503, provider error code SlowDown)"));
    assertNull(CustomerConfig.get(customer.getUuid(), config.getConfigUUID()));
  }

  @Test
  public void everyFieldErrorIsReported() {
    CustomerConfig config = validatingNfs("two", "/mnt");
    PlatformServiceException twoFields =
        new PlatformServiceException(
            400,
            Json.toJson(
                ImmutableMap.of(
                    "data.BACKUP_LOCATION",
                    List.of("bad location"),
                    "data.REGION_LOCATIONS[0].LOCATION",
                    List.of("bad region location"))));
    ValidateStorageConfig task =
        task(failingWith(twoFields), params(customer.getUuid(), config.getConfigUUID()));

    TaskExecutionException e = assertThrows(TaskExecutionException.class, task::run);
    assertTrue(e.getMessage(), e.getMessage().startsWith("STORAGE_CONFIG_INVALID: "));
    assertTrue(e.getMessage(), e.getMessage().contains("storage.backup_location: bad location"));
    assertTrue(e.getMessage(), e.getMessage().contains("bad region location"));
  }

  @Test
  public void unexpectedFailure() {
    CustomerConfig config = validatingNfs("bug", "/mnt");
    ValidateStorageConfig task =
        task(
            failingWith(new IllegalStateException("bug")),
            params(customer.getUuid(), config.getConfigUUID()));

    TaskExecutionException e = assertThrows(TaskExecutionException.class, task::run);
    assertEquals(Code.INTERNAL_ERROR, e.getCode());
    assertTrue(e.getMessage(), e.getMessage().contains("Unexpected error checking the storage"));
    assertNull(CustomerConfig.get(customer.getUuid(), config.getConfigUUID()));
  }

  @Test
  public void configThatLeftValidatingIsKept() {
    // Queued for deletion through v1 while the check ran.
    CustomerConfig config = validatingNfs("deleted", "relative/path");
    config.setState(ConfigState.QueuedForDeletion);
    config.update();

    assertThrows(
        TaskExecutionException.class,
        () -> task(params(customer.getUuid(), config.getConfigUUID())).run());
    config.refresh();
    assertEquals(ConfigState.QueuedForDeletion, config.getState());
  }

  @Test
  public void skipValidationIsHonored() {
    RuntimeConfigEntry.upsertGlobal(GlobalConfKeys.skipStorageConfigValidation.getKey(), "true");
    CustomerConfig config = validatingNfs("nfs", "relative/path");
    task(params(customer.getUuid(), config.getConfigUUID())).run();
    config.refresh();
    assertEquals(ConfigState.Active, config.getState());
  }

  @Test
  public void dryRun() {
    // Built as the handler builds it: unsaved.
    CustomerConfig good =
        CustomerConfig.createStorageConfig(
            customer.getUuid(), "NFS", "dry", Json.parse("{\"BACKUP_LOCATION\": \"/mnt\"}"));
    UUID key = unsavedConfigs.put(good);
    ValidateStorageConfig.Params params = params(customer.getUuid(), key);
    params.dryRun = true;
    task(params).run();
    assertNull("the task forgets the config", unsavedConfigs.get(key));
    assertNull(CustomerConfig.get(customer.getUuid(), "dry"));

    CustomerConfig bad =
        CustomerConfig.createStorageConfig(
            customer.getUuid(), "NFS", "dry", Json.parse("{\"BACKUP_LOCATION\": \"relative\"}"));
    UUID badKey = unsavedConfigs.put(bad);
    ValidateStorageConfig.Params badParams = params(customer.getUuid(), badKey);
    badParams.dryRun = true;
    TaskExecutionException e =
        assertThrows(TaskExecutionException.class, () -> task(badParams).run());
    assertTrue(e.getMessage(), e.getMessage().startsWith("STORAGE_CONFIG_INVALID: "));
    assertNull(unsavedConfigs.get(badKey));
  }

  @Test
  public void dryRunWithoutTheConfigFails() {
    // As after a YBA restart: the config was only in memory.
    ValidateStorageConfig.Params params = params(customer.getUuid(), UUID.randomUUID());
    params.dryRun = true;
    TaskExecutionException e = assertThrows(TaskExecutionException.class, () -> task(params).run());
    assertEquals(Code.INTERNAL_ERROR, e.getCode());
    assertTrue(e.getMessage(), e.getMessage().contains("YBA restarted"));
  }
}
