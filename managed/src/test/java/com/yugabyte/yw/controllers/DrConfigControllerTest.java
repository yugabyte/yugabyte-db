package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.DrConfigCreateForm;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams;
import com.yugabyte.yw.forms.XClusterConfigRestartFormData.RestartBootstrapParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class DrConfigControllerTest extends FakeDBApplication {
  private Universe sourceUniverse;
  private Universe targetUniverse;
  private Customer defaultCustomer;
  private Users user;
  private String authToken;
  private CustomerConfig config;
  private BootstarpBackupParams backupRequestParams;
  private RuntimeConfGetter mockConfGetter = mock(RuntimeConfGetter.class);
  private RuntimeConfGetter confGetter;
  private SettableRuntimeConfigFactory settableRuntimeConfigFactory;

  private CustomerConfig createData(Customer customer) {
    JsonNode formData =
        Json.parse(
            "{\"name\": \"Test\", \"configName\": \"Test\", \"type\": "
                + "\"STORAGE\", \"data\": {\"foo\": \"bar\"},"
                + "\"configUUID\": \"5e8e4887-343b-47dd-a126-71c822904c06\"}");
    return CustomerConfig.createWithFormData(customer.getUuid(), formData);
  }

  private DrConfigCreateForm createDefaultCreateForm(String name, Boolean dbScoped) {
    DrConfigCreateForm createForm = new DrConfigCreateForm();
    createForm.name = name;
    createForm.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    createForm.targetUniverseUUID = targetUniverse.getUniverseUUID();
    createForm.dbs = new HashSet<String>(Arrays.asList("db1"));
    createForm.bootstrapParams = new RestartBootstrapParams();
    createForm.bootstrapParams.backupRequestParams = backupRequestParams;

    if (dbScoped != null) {
      createForm.dbScoped = dbScoped;
    }

    return createForm;
  }

  @Before
  public void setUp() {
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    settableRuntimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.xcluster.dr.enabled", "true");
    defaultCustomer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(defaultCustomer);
    authToken = user.createAuthToken();
    config = createData(defaultCustomer);
    sourceUniverse = createUniverse("source Universe");
    targetUniverse = createUniverse("target Universe");

    backupRequestParams = new BootstarpBackupParams();
    backupRequestParams.storageConfigUUID = config.getConfigUUID();
  }

  @Test
  // Runtime config `yb.xcluster.db_scoped.enabled` = true and db scoped parameter is passed in
  // as true for request body.
  public void testCreateDbScopedSuccess() {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.xcluster.db_scoped.enabled", "true");
    DrConfigCreateForm data = createDefaultCreateForm("dbScopedDR", true);
    UUID taskUUID = buildTaskInfo(null, TaskType.CreateDrConfig);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + defaultCustomer.getUuid() + "/dr_configs",
            authToken,
            Json.toJson(data));

    assertOk(result);
    List<DrConfig> drConfigs =
        DrConfig.getBetweenUniverses(
            sourceUniverse.getUniverseUUID(), targetUniverse.getUniverseUUID());
    assertEquals(1, drConfigs.size());
    DrConfig drConfig = drConfigs.get(0);
    assertNotNull(drConfig);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    assertEquals(xClusterConfig.getType(), ConfigType.Db);
  }

  @Test
  // Runtime config `yb.xcluster.db_scoped.enabled` is disabled but db scoped parameter is passed in
  // as true for request body.
  public void testCreateDbScopedDisabledFailure() {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue("yb.xcluster.db_scoped.enabled", "false");
    DrConfigCreateForm data = createDefaultCreateForm("dbScopedDR", true);
    UUID taskUUID = buildTaskInfo(null, TaskType.CreateDrConfig);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    "/api/customers/" + defaultCustomer.getUuid() + "/dr_configs",
                    authToken,
                    Json.toJson(data)));

    assertBadRequest(result, "db scoped disaster recovery configs is disabled");
  }
}
