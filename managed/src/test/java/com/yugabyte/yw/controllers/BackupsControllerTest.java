// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static org.junit.Assert.*;
import static play.test.Helpers.contentAsString;

public class BackupsControllerTest extends FakeDBApplication {

  private Universe defaultUniverse;
  private Customer defaultCustomer;
  private Backup defaultBackup;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());

    BackupTableParams backupTableParams = new BackupTableParams();
    backupTableParams.universeUUID = defaultUniverse.universeUUID;
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer);
    backupTableParams.storageConfigUUID = customerConfig.configUUID;
    defaultBackup = Backup.create(defaultCustomer.uuid, backupTableParams);
  }

  private JsonNode doRequest(UUID universeUUID) {
    String authToken = defaultCustomer.createAuthToken();
    String method = "GET";
    String url = "/api/customers/" + defaultCustomer.uuid + "/universes/" + universeUUID + "/backups";

    Result r = FakeApiHelper.doRequestWithAuthToken(method, url, authToken);
    assertOk(r);
    return Json.parse(contentAsString(r));
  }

  @Test
  public void testListWithValidUniverse() {
    JsonNode resultJson = doRequest(defaultUniverse.universeUUID);
    assertEquals(1, resultJson.size());
    assertValues(resultJson, "backupUUID", ImmutableList.of(defaultBackup.backupUUID.toString()));
  }

  @Test
  public void testListWithInvalidUniverse() {
    JsonNode resultJson = doRequest(UUID.randomUUID());
    assertEquals(0, resultJson.size());
  }
}
