// Copyright 2026 YugabyteDB, Inc. and Contributors

package com.yugabyte.yw.common.config;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import io.ebean.DB;
import io.ebean.Transaction;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import org.junit.Test;

public class RuntimeConfigCacheTest extends FakeDBApplication {

  private static final String KEY = GlobalConfKeys.conditionalGetEnabled.getKey();

  @Test
  public void testFlagChangeVisibleAfterReadBeforeCommit() throws Exception {
    Customer customer = ModelFactory.testCustomer();
    RuntimeConfService runtimeConfService = app.injector().instanceOf(RuntimeConfService.class);
    RuntimeConfigCache runtimeConfigCache = app.injector().instanceOf(RuntimeConfigCache.class);
    // application.test.conf turns the flag on.
    assertTrue(runtimeConfigCache.getBoolean(KEY));

    ExecutorService otherRequest = Executors.newSingleThreadExecutor();
    try {
      try (Transaction transaction = DB.beginTransaction()) {
        runtimeConfService.setKey(customer.getUuid(), GLOBAL_SCOPE_UUID, KEY, "false", true);
        // Another request reads the flag before the change commits and still sees the old value.
        assertTrue(
            otherRequest
                .submit(() -> runtimeConfigCache.getBoolean(KEY))
                .get(30, TimeUnit.SECONDS));
        transaction.commit();
      }

      assertFalse(runtimeConfigCache.getBoolean(KEY));
      assertFalse(
          otherRequest.submit(() -> runtimeConfigCache.getBoolean(KEY)).get(30, TimeUnit.SECONDS));
    } finally {
      otherRequest.shutdownNow();
    }
  }
}
