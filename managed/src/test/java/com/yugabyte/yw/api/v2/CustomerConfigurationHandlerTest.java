// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.junit.Assert.assertThrows;

import api.v2.handlers.CustomerConfigurationHandler;
import api.v2.models.CustomerConfigPagedQuerySpec;
import api.v2.models.CustomerConfigPagedResp;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.AlertingData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class CustomerConfigurationHandlerTest extends FakeDBApplication {

  private Customer customer;
  private CustomerConfigurationHandler handler;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    handler = app.injector().instanceOf(CustomerConfigurationHandler.class);

    ObjectNode smtpData = Json.newObject();
    smtpData.put("smtpUsername", "u");
    smtpData.put("smtpServer", "smtp.example.test");
    CustomerConfig.createSmtpConfig(customer.getUuid(), smtpData).save();

    AlertingData alertingData = new AlertingData();
    alertingData.alertingEmail = "alerts@example.test";
    alertingData.sendAlertsToYb = false;
    alertingData.reportOnlyErrors = false;
    CustomerConfig.createAlertConfig(customer.getUuid(), Json.toJson(alertingData)).save();
  }

  @Test
  public void pageListCustomerConfigs_returnsPagedRows() {
    CustomerConfigPagedQuerySpec spec = new CustomerConfigPagedQuerySpec();
    spec.offset(0).limit(10);

    CustomerConfigPagedResp resp = handler.pageListCustomerConfigs(customer.getUuid(), spec);

    assertThat(resp.getTotalCount(), greaterThanOrEqualTo(2));
    assertThat(resp.getEntities().size(), greaterThanOrEqualTo(2));
  }

  @Test
  public void pageListCustomerConfigs_invalidCustomer() {
    CustomerConfigPagedQuerySpec spec = new CustomerConfigPagedQuerySpec();
    spec.offset(0).limit(10);

    assertThrows(
        PlatformServiceException.class,
        () -> handler.pageListCustomerConfigs(UUID.randomUUID(), spec));
  }

  @Test
  public void pageListCustomerConfigs_invalidPagination() {
    CustomerConfigPagedQuerySpec spec = new CustomerConfigPagedQuerySpec();
    spec.offset(-1).limit(10);

    assertThrows(
        PlatformServiceException.class,
        () -> handler.pageListCustomerConfigs(customer.getUuid(), spec));
  }
}
