// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

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
}
