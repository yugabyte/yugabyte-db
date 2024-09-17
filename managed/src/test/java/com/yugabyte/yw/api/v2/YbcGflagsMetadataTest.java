// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static play.mvc.Http.Status.OK;

import com.yugabyte.yba.v2.client.ApiClient;
import com.yugabyte.yba.v2.client.ApiResponse;
import com.yugabyte.yba.v2.client.Configuration;
import com.yugabyte.yba.v2.client.api.BackupAndRestoreApi;
import com.yugabyte.yba.v2.client.models.GflagMetadata;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.util.List;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class YbcGflagsMetadataTest extends FakeDBApplication {

  private Customer customer;
  private String authToken;
  private ApiClient v2ApiClient;
  private BackupAndRestoreApi api;

  private Users user;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    api = new BackupAndRestoreApi();
    authToken = user.createAuthToken();
    v2ApiClient = Configuration.getDefaultApiClient();
    String basePath = String.format("http://localhost:%d/api/v2", port);
    v2ApiClient = v2ApiClient.setBasePath(basePath).addDefaultHeader("X-AUTH-TOKEN", authToken);
    Configuration.setDefaultApiClient(v2ApiClient);
  }

  @Test
  public void testListYbcGflagsMetadata() throws Exception {
    ApiResponse<List<GflagMetadata>> res = api.listYbcGflagsMetadataWithHttpInfo();
    assertEquals(OK, res.getStatusCode());
    List<GflagMetadata> list = res.getData();
    assertNotNull(list);
    assertFalse(list.isEmpty());
  }
}
