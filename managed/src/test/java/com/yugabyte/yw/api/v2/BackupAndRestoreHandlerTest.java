// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import api.v2.handlers.BackupAndRestoreHandler;
import api.v2.models.BackupPagedQuerySpec;
import api.v2.models.BackupPagedResp;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class BackupAndRestoreHandlerTest extends FakeDBApplication {

  private Customer customer;
  private Universe universe;
  private BackupAndRestoreHandler handler;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    handler = app.injector().instanceOf(BackupAndRestoreHandler.class);
  }

  @Test
  public void pageListBackups_returnsPagedRows() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(customer, "backup-storage");
    BackupTableParams params = new BackupTableParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.storageConfigUUID = customerConfig.getConfigUUID();
    params.customerUuid = customer.getUuid();
    Backup backup = Backup.create(customer.getUuid(), params);
    backup.save();

    BackupPagedQuerySpec spec = new BackupPagedQuerySpec();
    spec.offset(0).limit(10);

    BackupPagedResp resp = handler.pageListBackups(customer.getUuid(), spec);

    assertThat(resp.getTotalCount(), greaterThanOrEqualTo(1));
    assertThat(resp.getEntities().size(), greaterThanOrEqualTo(1));
    assertEquals(backup.getBackupUUID(), resp.getEntities().get(0).getInfo().getUuid());
    assertEquals(universe.getUniverseUUID(), resp.getEntities().get(0).getSpec().getUniverseUuid());
  }

  @Test
  public void pageListBackups_invalidCustomer() {
    BackupPagedQuerySpec spec = new BackupPagedQuerySpec();
    spec.offset(0).limit(10);

    assertThrows(
        PlatformServiceException.class, () -> handler.pageListBackups(UUID.randomUUID(), spec));
  }
}
