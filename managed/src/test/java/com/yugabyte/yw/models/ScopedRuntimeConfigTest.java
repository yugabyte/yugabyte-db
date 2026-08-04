package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class ScopedRuntimeConfigTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private Provider defaultProvider;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
  }

  @Test
  public void testEnsureGetCascadeDelete_globalConfig() {
    ScopedRuntimeConfig.ensureGlobal();
    UUID uuid = GLOBAL_SCOPE_UUID;
    ScopedRuntimeConfig config = ScopedRuntimeConfig.get(uuid);
    assertEquals(uuid, config.getUuid());
    assertNull(config.getCustomerUUID());
    assertNull(config.getUniverseUUID());
    assertNull(config.getProviderUUID());
  }

  @Test
  public void testEnsureGetCascadeDelete_customerConfig() {
    ScopedRuntimeConfig.ensure(defaultCustomer);
    UUID uuid = defaultCustomer.getUuid();
    ScopedRuntimeConfig config = ScopedRuntimeConfig.get(uuid);
    assertEquals(uuid, config.getUuid());
    assertEquals(uuid, config.getCustomerUUID());
    assertNull(config.getUniverseUUID());
    assertNull(config.getProviderUUID());

    assertTrue(defaultCustomer.deletePermanent());
    assertNull(ScopedRuntimeConfig.get(uuid));
  }

  @Test
  public void testEnsureGetCascadeDelete_universeConfig() {
    ScopedRuntimeConfig.ensure(defaultUniverse);
    UUID uuid = defaultUniverse.getUniverseUUID();
    ScopedRuntimeConfig config = ScopedRuntimeConfig.get(uuid);
    assertEquals(uuid, config.getUuid());
    assertEquals(uuid, config.getUniverseUUID());
    assertNull(config.getCustomerUUID());
    assertNull(config.getProviderUUID());

    assertTrue(defaultUniverse.deletePermanent());
    assertNull(ScopedRuntimeConfig.get(uuid));
  }

  @Test
  public void testEnsureGetCascadeDelete_providerConfig() {
    ScopedRuntimeConfig.ensure(defaultProvider);
    UUID uuid = defaultProvider.getUuid();
    ScopedRuntimeConfig config = ScopedRuntimeConfig.get(uuid);
    assertEquals(uuid, config.getUuid());
    assertEquals(uuid, config.getProviderUUID());
    assertNull(config.getCustomerUUID());
    assertNull(config.getUniverseUUID());

    assertTrue(defaultProvider.deletePermanent());
    assertNull(ScopedRuntimeConfig.get(uuid));
  }
}
