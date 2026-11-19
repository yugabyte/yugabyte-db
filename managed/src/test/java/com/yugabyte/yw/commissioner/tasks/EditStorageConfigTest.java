// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.common.customer.config.StorageCheckRunner;
import com.yugabyte.yw.common.customer.config.UnsavedStorageConfigs;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class EditStorageConfigTest extends FakeDBApplication {

  private Customer customer;
  private UnsavedStorageConfigs unsavedConfigs;
  private CustomerConfig stored;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    unsavedConfigs = app.injector().instanceOf(UnsavedStorageConfigs.class);
    stored = ModelFactory.createNfsStorageConfig(customer, "nfs", "/mnt/nfs");
  }

  /** A task with the real storage validators (NFS ones check the path without any storage). */
  private EditStorageConfig task(EditStorageConfig.Params params) {
    return task(app.injector().instanceOf(CustomerConfigValidator.class), params);
  }

  private EditStorageConfig task(
      CustomerConfigValidator validator, EditStorageConfig.Params params) {
    EditStorageConfig task =
        new EditStorageConfig(
            app.injector().instanceOf(BaseTaskDependencies.class),
            validator,
            unsavedConfigs,
            app.injector().instanceOf(StorageCheckRunner.class));
    task.initialize(params);
    return task;
  }

  /** The stored config renamed, with its data changed by {@code data} (null: unchanged). */
  private CustomerConfig edit(String name, ObjectNode data) {
    return new CustomerConfig()
        .setConfigUUID(stored.getConfigUUID())
        .setCustomerUUID(customer.getUuid())
        .setType(ConfigType.STORAGE)
        .setName(stored.getName())
        .setConfigName(name)
        .setData(data != null ? data : stored.getData().deepCopy());
  }

  /** The stored data with a region location, which the NFS validator checks. */
  private ObjectNode withRegion(String location) {
    ObjectNode data = stored.getData().deepCopy();
    ObjectNode region = Json.newObject().put("REGION", "us-west-2").put("LOCATION", location);
    data.putArray("REGION_LOCATIONS").add(region);
    return data;
  }

  /** Params of the edit, requested now: the revision is the stored config's. */
  private EditStorageConfig.Params params(CustomerConfig edited) {
    EditStorageConfig.Params params = new EditStorageConfig.Params();
    params.customerUUID = customer.getUuid();
    params.configUUID = stored.getConfigUUID();
    params.editKey = unsavedConfigs.put(edited);
    params.revision = CustomerConfig.get(stored.getConfigUUID()).getUpdateTime().getTime();
    return params;
  }

  private static CustomerConfigValidator failingValidator() {
    CustomerConfigValidator validator = mock(CustomerConfigValidator.class);
    doThrow(new AssertionError("the storage check ran"))
        .when(validator)
        .validateConfigDataWithValidator(any(), any());
    return validator;
  }

  private CustomerConfig reloaded() {
    return CustomerConfig.get(customer.getUuid(), stored.getConfigUUID());
  }

  @Test
  public void renameSkipsTheStorageCheck() {
    long before = reloaded().getUpdateTime().getTime();
    EditStorageConfig.Params params = params(edit("renamed", null));

    task(failingValidator(), params).run();

    CustomerConfig after = reloaded();
    assertEquals("renamed", after.getConfigName());
    assertEquals(ConfigState.Active, after.getState());
    assertTrue(after.getUpdateTime().getTime() >= before);
    assertNull(unsavedConfigs.get(params.editKey));
  }

  @Test
  public void passingCheckAppliesTheEdit() {
    task(params(edit("nfs", withRegion("/mnt/west")))).run();

    assertEquals(
        "/mnt/west", reloaded().getData().get("REGION_LOCATIONS").get(0).get("LOCATION").asText());
  }

  @Test
  public void failingCheckLeavesTheConfigAsItIs() {
    EditStorageConfig.Params params = params(edit("renamed", withRegion("relative/path")));

    TaskExecutionException e = assertThrows(TaskExecutionException.class, task(params)::run);

    assertTrue(e.getMessage(), e.getMessage().startsWith("STORAGE_CONFIG_INVALID: "));
    CustomerConfig after = reloaded();
    assertEquals("nfs", after.getConfigName());
    assertTrue(after.getData().path("REGION_LOCATIONS").isMissingNode());
    assertEquals(ConfigState.Active, after.getState());
    assertNull(unsavedConfigs.get(params.editKey));
  }

  @Test
  public void changeBeforeTheTaskIsNotOverwritten() throws InterruptedException {
    EditStorageConfig.Params params = params(edit("renamed", null));
    // update_time has millisecond resolution: a save within the same millisecond as the create
    // wouldn't advance it.
    Thread.sleep(5);
    // A v1 edit, after the v2 edit was requested.
    CustomerConfig v1 = reloaded();
    v1.setConfigName("v1-edit");
    v1.update();

    TaskExecutionException e = assertThrows(TaskExecutionException.class, task(params)::run);

    assertTrue(e.getMessage(), e.getMessage().startsWith("STORAGE_CONFIG_CHANGED: "));
    assertEquals("v1-edit", reloaded().getConfigName());
  }

  @Test
  public void changeDuringTheCheckIsNotOverwritten() {
    EditStorageConfig.Params params = params(edit("nfs", withRegion("/mnt/west")));
    CustomerConfigValidator validator = mock(CustomerConfigValidator.class);
    doAnswer(
            invocation -> {
              // Queued for deletion through v1 while the storage was being checked.
              reloaded().updateState(ConfigState.QueuedForDeletion);
              return null;
            })
        .when(validator)
        .validateConfigDataWithValidator(any(), any());

    TaskExecutionException e =
        assertThrows(TaskExecutionException.class, task(validator, params)::run);

    assertTrue(e.getMessage(), e.getMessage().startsWith("STORAGE_CONFIG_CHANGED: "));
    CustomerConfig after = reloaded();
    assertEquals(ConfigState.QueuedForDeletion, after.getState());
    assertTrue(after.getData().path("REGION_LOCATIONS").isMissingNode());
  }

  @Test
  public void nameTakenMeanwhileFails() {
    EditStorageConfig.Params params = params(edit("taken", null));
    ModelFactory.createNfsStorageConfig(customer, "taken");

    TaskExecutionException e =
        assertThrows(TaskExecutionException.class, task(failingValidator(), params)::run);

    assertTrue(e.getMessage(), e.getMessage().startsWith("STORAGE_CONFIG_NAME_CONFLICT: "));
    assertEquals("nfs", reloaded().getConfigName());
  }

  @Test
  public void missingEditFails() {
    EditStorageConfig.Params params = params(edit("renamed", null));
    unsavedConfigs.remove(params.editKey);

    assertThrows(TaskExecutionException.class, task(params)::run);
    assertEquals("nfs", reloaded().getConfigName());
  }

  @Test
  public void otherCustomersConfigIsNotEdited() {
    EditStorageConfig.Params params = params(edit("renamed", null));
    params.customerUUID = UUID.randomUUID();

    assertThrows(TaskExecutionException.class, task(params)::run);
    assertEquals("nfs", reloaded().getConfigName());
  }
}
