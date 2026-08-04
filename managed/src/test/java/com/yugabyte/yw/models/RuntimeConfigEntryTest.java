package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.junit.Before;
import org.junit.Test;

public class RuntimeConfigEntryTest extends FakeDBApplication {

  public static final String YB_SB_START_YEAR_KEY = "yb.sb.startYear";
  public static final String YB_SB_TIMEZONE_KEY = "yb.sb.timezone";
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
  public void testCreateLookupGetAllValidScopeCascadeDelete_global() {
    UUID scopeUuid = ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;
    RuntimeConfigEntry.upsertGlobal(YB_SB_START_YEAR_KEY, "1886");
    checkStartYear(scopeUuid, "1886");
    // Make sure that the config with same scope and path is unique and later update overwrites
    // the former.
    RuntimeConfigEntry.upsertGlobal(YB_SB_START_YEAR_KEY, "2020");
    checkStartYear(scopeUuid, "2020");

    RuntimeConfigEntry.upsertGlobal(YB_SB_TIMEZONE_KEY, "PST");
    checkAll(scopeUuid);

    ScopedRuntimeConfig scopedRuntimeConfig = ScopedRuntimeConfig.get(scopeUuid);
    assertEquals(scopeUuid, scopedRuntimeConfig.getUuid());
    assertNull(scopedRuntimeConfig.getCustomerUUID());
    assertNull(scopedRuntimeConfig.getProviderUUID());
    assertNull(scopedRuntimeConfig.getUniverseUUID());

    scopedRuntimeConfig.deletePermanent();
    checkCascadeDelete(scopeUuid);
  }

  @Test
  public void testCreateLookupGetAllValidScopeCascadeDelete_customer() {
    UUID scopeUuid = defaultCustomer.getUuid();
    RuntimeConfigEntry.upsert(defaultCustomer, YB_SB_START_YEAR_KEY, "1857");
    checkStartYear(scopeUuid, "1857");
    // Make sure that the config with same scope and path is unique and later update overwrites
    // the former.
    RuntimeConfigEntry.upsert(defaultCustomer, YB_SB_START_YEAR_KEY, "2020");
    checkStartYear(scopeUuid, "2020");

    RuntimeConfigEntry.upsert(defaultCustomer, YB_SB_TIMEZONE_KEY, "PST");
    checkAll(scopeUuid);

    ScopedRuntimeConfig scopedRuntimeConfig = ScopedRuntimeConfig.get(scopeUuid);
    assertEquals(scopeUuid, scopedRuntimeConfig.getUuid());
    assertEquals(scopeUuid, scopedRuntimeConfig.getCustomerUUID());
    assertNull(scopedRuntimeConfig.getProviderUUID());
    assertNull(scopedRuntimeConfig.getUniverseUUID());

    defaultCustomer.deletePermanent();
    checkCascadeDelete(scopeUuid);
  }

  @Test
  public void testCreateLookupGetAllValidScopeCascadeDelete_universe() {
    UUID scopeUuid = defaultUniverse.getUniverseUUID();
    RuntimeConfigEntry.upsert(defaultUniverse, YB_SB_START_YEAR_KEY, "2021");
    checkStartYear(scopeUuid, "2021");
    // Make sure that the config with same scope and path is unique and later update overwrites
    // the former.
    RuntimeConfigEntry.upsert(defaultUniverse, YB_SB_START_YEAR_KEY, "2020");
    checkStartYear(scopeUuid, "2020");

    RuntimeConfigEntry.upsert(defaultUniverse, YB_SB_TIMEZONE_KEY, "PST");
    checkAll(scopeUuid);

    ScopedRuntimeConfig scopedRuntimeConfig = ScopedRuntimeConfig.get(scopeUuid);
    assertEquals(scopeUuid, scopedRuntimeConfig.getUuid());
    assertEquals(scopeUuid, scopedRuntimeConfig.getUniverseUUID());
    assertNull(scopedRuntimeConfig.getProviderUUID());
    assertNull(scopedRuntimeConfig.getCustomerUUID());

    defaultUniverse.deletePermanent();
    checkCascadeDelete(scopeUuid);
  }

  @Test
  public void testCreateLookupGetAllValidScopeCascadeDelete_provider() {
    UUID scopeUuid = defaultProvider.getUuid();
    RuntimeConfigEntry.upsert(defaultProvider, YB_SB_START_YEAR_KEY, "1984");
    checkStartYear(scopeUuid, "1984");
    // Make sure that the config with same scope and path is unique and later update overwrites
    // the former.
    RuntimeConfigEntry.upsert(defaultProvider, YB_SB_START_YEAR_KEY, "2020");
    checkStartYear(scopeUuid, "2020");

    RuntimeConfigEntry.upsert(defaultProvider, YB_SB_TIMEZONE_KEY, "PST");
    checkAll(scopeUuid);

    ScopedRuntimeConfig scopedRuntimeConfig = ScopedRuntimeConfig.get(scopeUuid);
    assertEquals(scopeUuid, scopedRuntimeConfig.getUuid());
    assertEquals(scopeUuid, scopedRuntimeConfig.getProviderUUID());
    assertNull(scopedRuntimeConfig.getUniverseUUID());
    assertNull(scopedRuntimeConfig.getCustomerUUID());

    defaultProvider.deletePermanent();
    checkCascadeDelete(scopeUuid);
  }

  @Test
  public void testCreateEntryWithHugeValues() {
    UUID scopeUuid = defaultCustomer.getUuid();
    String longString =
        Stream.generate(() -> "\u4F60\u597D")
            .limit(1024 * 64 / 2 + 1)
            .collect(Collectors.joining());
    RuntimeConfigEntry.upsert(defaultCustomer, YB_SB_START_YEAR_KEY, longString);
    checkStartYear(scopeUuid, longString);
  }

  private void checkCascadeDelete(UUID scopeUuid) {
    assertNull(ScopedRuntimeConfig.get(scopeUuid));
    assertNull(RuntimeConfigEntry.get(scopeUuid, YB_SB_START_YEAR_KEY));
    assertTrue(RuntimeConfigEntry.getAll(scopeUuid).isEmpty());
  }

  private void checkStartYear(UUID scopeUuid, String expected) {
    RuntimeConfigEntry entry = RuntimeConfigEntry.get(scopeUuid, YB_SB_START_YEAR_KEY);
    assertNotNull(entry);
    assertEquals(YB_SB_START_YEAR_KEY, entry.getPath());
    assertEquals(expected, entry.getValue());
  }

  private void checkAll(UUID scopeUuid) {
    List<RuntimeConfigEntry> entryList = RuntimeConfigEntry.getAll(scopeUuid);
    assertSame(2, entryList.size());
    entryList.forEach(
        runtimeConfigEntry -> {
          switch (runtimeConfigEntry.getPath()) {
            case YB_SB_START_YEAR_KEY:
              assertEquals("2020", runtimeConfigEntry.getValue());
              break;
            case YB_SB_TIMEZONE_KEY:
              assertEquals("PST", runtimeConfigEntry.getValue());
              break;
            default:
              fail(
                  "Unexpected path "
                      + runtimeConfigEntry.getPath()
                      + "for config entry: "
                      + runtimeConfigEntry);
          }
        });
  }
}
