// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.RunQueryFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CreateYbStorageDatabaseTest extends FakeDBApplication {

  private CreateYbStorageDatabase task;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    Universe universe = ModelFactory.createUniverse("universe", customer.getId());
    Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    task = app.injector().instanceOf(CreateYbStorageDatabase.class);
    CreateYbStorageDatabase.Params params = new CreateYbStorageDatabase.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    task.initialize(params);
  }

  private static JsonNode ok() {
    return Json.newObject().put("result", "CREATE DATABASE");
  }

  private static JsonNode error(String msg) {
    return Json.newObject().put("error", msg);
  }

  private void whenCreate(JsonNode first, JsonNode... rest) {
    when(mockYsqlQueryExecutor.executeQueryInNodeShell(
            any(),
            argThat(
                (RunQueryFormData q) ->
                    q != null && q.getQuery().equals("CREATE DATABASE yb_storage")),
            any()))
        .thenReturn(first, rest);
  }

  private void verifyAttempts(int n) {
    verify(mockYsqlQueryExecutor, times(n)).executeQueryInNodeShell(any(), any(), any());
  }

  @Test
  public void testCreatesWhenMissing() {
    whenCreate(ok());
    task.run();
    verifyAttempts(1);
  }

  @Test
  public void testExistingDatabaseIsSuccess() {
    whenCreate(error("ERROR:  database \"yb_storage\" already exists"));
    task.run();
    verifyAttempts(1);
  }

  @Test
  public void testRetriesThenSucceeds() {
    whenCreate(error("connection refused"), ok());
    task.run();
    verifyAttempts(2);
  }

  @Test
  public void testFailsAfterRetries() {
    whenCreate(error("timed out"));
    assertThrows(PlatformServiceException.class, () -> task.run());
    verifyAttempts(5);
  }
}
