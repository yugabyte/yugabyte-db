// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static org.junit.Assert.assertEquals;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.rbac.PermissionInfo;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.PermissionUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.io.File;
import java.io.IOException;
import java.util.List;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.Environment;
import play.Mode;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class RBACControllerTest extends FakeDBApplication {

  private Customer customer;
  private Users user;
  private PermissionUtil permissionUtil;

  private Environment environment;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    this.environment = new Environment(new File("."), classLoader, Mode.TEST);
    this.permissionUtil = new PermissionUtil(environment);
  }

  @After
  public void tearDown() throws IOException {}

  /* ==== Helper Request Functions ==== */

  private Result listPermissionsAPI(UUID customerUUID, String resourceType) {
    String uri = "/api/customers/%s/rbac/permissions?resourceType=%s";
    return doRequestWithAuthToken(
        "GET", String.format(uri, customerUUID.toString(), resourceType), user.createAuthToken());
  }

  /* ==== List Permissions API ==== */

  @Test
  public void testListDefaultPermissions() {
    Result result = listPermissionsAPI(customer.getUuid(), ResourceType.DEFAULT.toString());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());

    List<PermissionInfo> permissionInfoList = Json.fromJson(json, List.class);
    assertEquals(
        permissionInfoList.size(),
        permissionUtil.getAllPermissionInfo(ResourceType.DEFAULT).size());
  }
}
