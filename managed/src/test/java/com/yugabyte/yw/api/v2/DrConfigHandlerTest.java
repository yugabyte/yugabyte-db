// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.handlers.DrConfigHandler;
import api.v2.models.DrConfigDbDetailPagedQuerySpec;
import api.v2.models.DrConfigDbDetailPagedResp;
import api.v2.models.DrConfigPagedQuerySpec;
import api.v2.models.DrConfigPagedResp;
import api.v2.models.DrConfigTableDetailPagedQuerySpec;
import api.v2.models.DrConfigTableDetailPagedResp;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.DrConfigCreateForm.PitrParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterNamespaceConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class DrConfigHandlerTest extends FakeDBApplication {

  private static final String TABLE_ID = "000033df000030008000000000004005";
  private static final String NAMESPACE_ID = "111133df000030008000000000004006";

  private Customer customer;
  private Universe sourceUniverse;
  private DrConfigHandler handler;
  private DrConfig drConfig;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    sourceUniverse = ModelFactory.createUniverse("source-universe", customer.getId());
    Universe targetUniverse = ModelFactory.createUniverse("target-universe", customer.getId());
    handler = app.injector().instanceOf(DrConfigHandler.class);

    CustomerConfig storageConfig = ModelFactory.createNfsStorageConfig(customer, "dr-storage");
    BootstrapBackupParams backupRequestParams = new BootstrapBackupParams();
    backupRequestParams.storageConfigUUID = storageConfig.getConfigUUID();
    backupRequestParams.parallelism = 2;
    PitrParams pitrParams = new PitrParams();

    drConfig =
        DrConfig.create(
            "dr-test",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            Set.of(TABLE_ID),
            backupRequestParams,
            pitrParams);
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    xClusterConfig.addNamespaceConfig(new XClusterNamespaceConfig(xClusterConfig, NAMESPACE_ID));
    xClusterConfig.update();
  }

  @Test
  public void pageListDrConfigs_returnsPagedRows() {
    DrConfigPagedQuerySpec spec = new DrConfigPagedQuerySpec();
    spec.offset(0).limit(10);

    DrConfigPagedResp resp =
        handler.pageListDrConfigs(customer.getUuid(), sourceUniverse.getUniverseUUID(), spec);

    assertThat(resp.getTotalCount(), greaterThanOrEqualTo(1));
    assertThat(resp.getEntities().size(), greaterThanOrEqualTo(1));
  }

  @Test
  public void pageListDrConfigs_invalidCustomer() {
    DrConfigPagedQuerySpec spec = new DrConfigPagedQuerySpec();
    spec.offset(0).limit(10);

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                handler.pageListDrConfigs(
                    UUID.randomUUID(), sourceUniverse.getUniverseUUID(), spec));
    assertEquals(NOT_FOUND, exception.getHttpStatus());
  }

  @Test
  public void pageListDrConfigTables_returnsPagedRows() {
    DrConfigTableDetailPagedQuerySpec spec = new DrConfigTableDetailPagedQuerySpec();
    spec.offset(0).limit(10);

    DrConfigTableDetailPagedResp resp =
        handler.pageListDrConfigTables(customer.getUuid(), drConfig.getUuid(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(1, resp.getEntities().size());
    assertEquals(TABLE_ID, resp.getEntities().get(0).getTableId());
  }

  @Test
  public void pageListDrConfigDatabases_returnsPagedRows() {
    DrConfigDbDetailPagedQuerySpec spec = new DrConfigDbDetailPagedQuerySpec();
    spec.offset(0).limit(10);

    DrConfigDbDetailPagedResp resp =
        handler.pageListDrConfigDatabases(customer.getUuid(), drConfig.getUuid(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(1, resp.getEntities().size());
    assertEquals(NAMESPACE_ID, resp.getEntities().get(0).getSourceNamespaceId());
  }

  @Test
  public void pageListDrConfigTables_invalidDrConfig() {
    DrConfigTableDetailPagedQuerySpec spec = new DrConfigTableDetailPagedQuerySpec();
    spec.offset(0).limit(10);

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.pageListDrConfigTables(customer.getUuid(), UUID.randomUUID(), spec));
    assertEquals(NOT_FOUND, exception.getHttpStatus());
  }
}
