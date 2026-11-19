// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class CustomerConfigTest extends FakeDBApplication {

  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  private CustomerConfig createData(Customer customer) {
    JsonNode formData =
        Json.parse(
            "{\"name\": \"Test\", \"configName\": \"Test\", \"type\": "
                + "\"STORAGE\", \"data\": {\"foo\": \"bar\"}}");
    return CustomerConfig.createWithFormData(customer.getUuid(), formData);
  }

  @Test
  public void testCreateWithFormData() {
    assertEquals(0, CustomerConfig.find.all().size());
    createData(defaultCustomer);
    assertEquals(1, CustomerConfig.find.all().size());
  }

  @Test
  public void testGetAll() {
    createData(defaultCustomer);
    Customer newCustomer = ModelFactory.testCustomer("nc", "new customer");
    assertEquals(0, CustomerConfig.getAll(newCustomer.getUuid()).size());
    assertEquals(1, CustomerConfig.getAll(defaultCustomer.getUuid()).size());
  }

  @Test
  public void testGetData() {
    JsonNode formData =
        Json.parse(
            "{\"name\": \"Test\", \"configName\": \"Test\", \"type\": \"STORAGE\", "
                + "\"data\": {\"KEY\": \"ABCDEFGHIJ\", \"SECRET\": \"123456789\", "
                + "\"DATA\": \"HELLO\"}}");
    CustomerConfig customerConfig =
        CustomerConfig.createWithFormData(defaultCustomer.getUuid(), formData);

    JsonNode data = customerConfig.getData();
    assertValue(data, "KEY", "ABCDEFGHIJ");
    assertValue(data, "SECRET", "123456789");
    assertValue(data, "DATA", "HELLO");

    JsonNode maskedData = customerConfig.getMaskedData();
    assertValue(maskedData, "KEY", "AB******IJ");
    assertValue(maskedData, "SECRET", "12*****89");
    assertValue(maskedData, "DATA", "HELLO");
  }

  @Test
  public void testGetValidID() {
    CustomerConfig cc = createData(defaultCustomer);
    CustomerConfig fc = CustomerConfig.get(defaultCustomer.getUuid(), cc.getConfigUUID());
    assertNotNull(fc);
  }

  @Test
  public void testGetInvalidID() {
    Customer newCustomer = ModelFactory.testCustomer("nc", "new@customer.com");
    CustomerConfig cc = createData(newCustomer);
    CustomerConfig fc = CustomerConfig.get(defaultCustomer.getUuid(), cc.getConfigUUID());
    assertNull(fc);
  }

  @Test
  public void testDataAsMap() {
    CustomerConfig cc = createData(defaultCustomer);
    Map<String, String> data = cc.dataAsMap();
    assertEquals(1, data.size());
    assertEquals(ImmutableMap.of("foo", "bar"), data);
  }

  @Test
  public void testDeleteStorageConfigWithoutBackupAndSchedule() {
    CustomerConfig cc = createData(defaultCustomer);
    CustomerConfig fc = CustomerConfig.get(defaultCustomer.getUuid(), cc.getConfigUUID());
    assertNotNull(fc);
    fc.delete();
    fc = CustomerConfig.get(defaultCustomer.getUuid(), cc.getConfigUUID());
    assertNull(fc);
  }

  @Test
  public void onlyActiveConfigsAreActive() {
    CustomerConfig cc = createData(defaultCustomer);
    assertTrue(cc.isActive());
    cc.setState(CustomerConfig.ConfigState.Validating);
    assertFalse(cc.isActive());
    cc.setState(CustomerConfig.ConfigState.QueuedForDeletion);
    assertFalse(cc.isActive());
    // Not a field of the v1 config JSON.
    assertFalse(Json.toJson(cc).has("active"));
  }

  @Test
  public void updateTimeAdvancesOnEverySave() throws InterruptedException {
    CustomerConfig cc = createData(defaultCustomer);
    long created = CustomerConfig.get(cc.getConfigUUID()).getUpdateTime().getTime();
    // Millisecond resolution.
    Thread.sleep(5);
    cc.setConfigName("renamed");
    cc.update();
    assertTrue(CustomerConfig.get(cc.getConfigUUID()).getUpdateTime().getTime() > created);

    Thread.sleep(5);
    long renamed = CustomerConfig.get(cc.getConfigUUID()).getUpdateTime().getTime();
    cc.setState(CustomerConfig.ConfigState.Validating);
    cc.update();
    // Bulk state updates set it too.
    Thread.sleep(5);
    CustomerConfig.updateStateIfValidating(cc.getConfigUUID(), CustomerConfig.ConfigState.Active);
    assertTrue(CustomerConfig.get(cc.getConfigUUID()).getUpdateTime().getTime() > renamed);
    // Not part of the v1 config JSON.
    assertFalse(Json.toJson(cc).has("updateTime"));
  }
}
