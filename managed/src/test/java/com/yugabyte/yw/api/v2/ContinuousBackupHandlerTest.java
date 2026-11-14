// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import api.v2.handlers.ContinuousBackupHandler;
import api.v2.models.ContinuousBackupSpec;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import org.junit.Before;
import org.junit.Test;

public class ContinuousBackupHandlerTest extends FakeDBApplication {

  private Customer customer;
  private ContinuousBackupHandler handler;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    handler = app.injector().instanceOf(ContinuousBackupHandler.class);
  }

  @Test
  public void configBeingValidatedIsRefused() {
    // Created through v2 and still being checked: it is deleted if its check fails.
    CustomerConfig config = ModelFactory.createNfsStorageConfig(customer, "validating", "/mnt");
    config.setState(ConfigState.Validating);
    config.update();
    ContinuousBackupSpec spec =
        new ContinuousBackupSpec().storageConfigUuid(config.getConfigUUID()).backupDir("dir");

    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.createContinuousBackup(customer.getUuid(), spec));
    assertEquals(400, e.getHttpStatus());
    assertTrue(e.getMessage(), e.getMessage().contains("only an active storage config"));
    assertFalse(ContinuousBackupConfig.get().isPresent());
  }
}
