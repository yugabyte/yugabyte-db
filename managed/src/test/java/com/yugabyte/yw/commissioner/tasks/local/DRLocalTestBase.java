// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.DrConfigSetDatabasesForm;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class DRLocalTestBase extends XClusterLocalTestBase {

  protected Result createDrConfig(DrConfigCreateForm formData) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "POST",
        "/api/customers/" + customer.getUuid() + "/dr_configs",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  protected Result setDatabasesDrConfig(UUID drConfigUUID, DrConfigSetDatabasesForm formData) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "PUT",
        "/api/customers/" + customer.getUuid() + "/dr_configs/" + drConfigUUID + "/set_dbs",
        user.createAuthToken(),
        Json.toJson(formData));
  }

  protected Result deleteDrConfig(UUID drConfigUUID) {
    return FakeApiHelper.doRequestWithAuthToken(
        app,
        "DELETE",
        "/api/customers/" + customer.getUuid() + "/dr_configs/" + drConfigUUID,
        user.createAuthToken());
  }

  @Before
  public void setupDr() {
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.xcluster.dr.enabled", "true");
  }
}
