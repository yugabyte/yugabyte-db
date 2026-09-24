// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.google.protobuf.ByteString;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.CommonTypes;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.RelationType;

@RunWith(JUnitParamsRunner.class)
public class XClusterUtilTest extends FakeDBApplication {

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
  }

  @Test
  public void testCheckDbScopedXClusterSupportedSuccess() {
    Universe sourceUniverse = ModelFactory.createUniverse("source Universe");
    TestHelper.updateUniverseVersion(sourceUniverse, "2.23.0.0-b394");
    Universe targetUniverse = ModelFactory.createUniverse("target Universe");
    TestHelper.updateUniverseVersion(targetUniverse, "2.23.0.0-b394");
    try {
      XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse);
    } catch (Exception e) {
      fail("Source and target universe versions should be valid for db scoped");
    }

    TestHelper.updateUniverseVersion(sourceUniverse, "2024.1.3.0-b106");
    TestHelper.updateUniverseVersion(targetUniverse, "2024.1.3.0-b106");
    try {
      XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse);
    } catch (Exception e) {
      fail("Source and target universe versions should be valid for db scoped");
    }
  }

  @Test
  public void testCheckDbScopedXClusterSupportedFailure() {
    Universe sourceUniverse = ModelFactory.createUniverse("source Universe");
    Universe targetUniverse = ModelFactory.createUniverse("target Universe");

    // Source universe version does not support db scoped.
    TestHelper.updateUniverseVersion(sourceUniverse, "2.23.0.0-b393");
    TestHelper.updateUniverseVersion(targetUniverse, "2.23.0.0-b394");
    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse));

    // Target universe version does not support db scoped.
    TestHelper.updateUniverseVersion(sourceUniverse, "2.23.0.0-b395");
    TestHelper.updateUniverseVersion(targetUniverse, "2.21.0.0-b1");
    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse));

    // Source universe version does not support db scoped.
    TestHelper.updateUniverseVersion(sourceUniverse, "2024.1.3.0-b1");
    TestHelper.updateUniverseVersion(targetUniverse, "2024.1.3.0-b107");
    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse));

    // Target universe version does not support db scoped.
    TestHelper.updateUniverseVersion(sourceUniverse, "2024.4.1.0-b1");
    TestHelper.updateUniverseVersion(targetUniverse, "2024.1.3.0-b104");
    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse));
  }

  @Test
  public void testCheckDbScopedNonEmptyDb() {
    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedNonEmptyDbs(new HashSet<String>()));

    try {
      XClusterUtil.checkDbScopedNonEmptyDbs(Set.of("db1", "db2"));
    } catch (Exception e) {
      fail("Non-empty dbs should not throw error.");
    }
  }

  @Test
  public void testDbScopedXClusterSupportedOnUniverseInPreFinalizeState() {
    Universe sourceUniverse = ModelFactory.createUniverse("source Universe");
    Universe targetUniverse = ModelFactory.createUniverse("target Universe");

    // Source universe support db scoped xCluster.
    TestHelper.updateUniverseVersion(
        sourceUniverse, XClusterUtil.MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_STABLE);
    // Target universe did not support db scoped xCluster but is in pre finalize state to a version
    // that supports db scoped xCluster.
    TestHelper.updateUniverseVersion(
        targetUniverse, XClusterUtil.MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_STABLE);
    PrevYBSoftwareConfig prevYBSoftwareConfig = new PrevYBSoftwareConfig();
    prevYBSoftwareConfig.setSoftwareVersion("2024.1.3.0-b1");
    prevYBSoftwareConfig.setAutoFlagConfigVersion(4);
    TestHelper.updateUniversePrevSoftwareConfig(targetUniverse, prevYBSoftwareConfig);
    TestHelper.updateUniverseSoftwareUpgradeState(targetUniverse, SoftwareUpgradeState.PreFinalize);

    assertThrows(
        PlatformServiceException.class,
        () -> XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse));

    TestHelper.updateUniverseSoftwareUpgradeState(targetUniverse, SoftwareUpgradeState.Ready);
    try {
      XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse);
    } catch (Exception e) {
      fail("Source and target universe versions should be valid for db scoped");
    }

    TestHelper.updateUniverseVersion(sourceUniverse, "2024.1.3.0-b150");
    TestHelper.updateUniverseSoftwareUpgradeState(targetUniverse, SoftwareUpgradeState.PreFinalize);
    TestHelper.updateUniverseVersion(targetUniverse, "2024.1.3.0-b150");
    prevYBSoftwareConfig.setSoftwareVersion(
        XClusterUtil.MINIMUM_VERSION_DB_XCLUSTER_SUPPORT_STABLE);
    TestHelper.updateUniversePrevSoftwareConfig(targetUniverse, prevYBSoftwareConfig);

    try {
      XClusterUtil.checkDbScopedXClusterSupported(sourceUniverse, targetUniverse);
    } catch (Exception e) {
      fail("Source and target universe versions should be valid for db scoped");
    }
  }

  @Test
  public void testSupportsAutomaticDdlSuccess() {
    Universe universe = ModelFactory.createUniverse("test Universe");

    // Test with preview version that supports automatic DDL
    TestHelper.updateUniverseVersion(universe, "2.29.1.0-b1");
    assert XClusterUtil.supportsAutomaticDdl(universe) : "Universe should support automatic DDL";
    TestHelper.updateUniverseVersion(universe, "2.29.1.0-b100");
    assert XClusterUtil.supportsAutomaticDdl(universe) : "Universe should support automatic DDL";

    // Test with stable version that supports automatic DDL
    TestHelper.updateUniverseVersion(universe, "2025.2.1.0-b1");
    assert XClusterUtil.supportsAutomaticDdl(universe) : "Universe should support automatic DDL";
    TestHelper.updateUniverseVersion(universe, "2025.2.1.0-b200");
    assert XClusterUtil.supportsAutomaticDdl(universe) : "Universe should support automatic DDL";
  }

  @Test
  public void testSupportsAutomaticDdlFailure() {
    Universe universe = ModelFactory.createUniverse("test Universe");
    TestHelper.updateUniverseVersion(universe, "2.badversion");
    assert !XClusterUtil.supportsAutomaticDdl(universe)
        : "Universe should not support automatic DDL";

    // Test with preview version that doesn't support automatic DDL
    TestHelper.updateUniverseVersion(universe, "2.27.1.0-b0");
    assert !XClusterUtil.supportsAutomaticDdl(universe)
        : "Universe should not support automatic DDL";
    TestHelper.updateUniverseVersion(universe, "2.25.0.0-b100");
    assert !XClusterUtil.supportsAutomaticDdl(universe)
        : "Universe should not support automatic DDL";

    // Test with stable version that doesn't support automatic DDL
    TestHelper.updateUniverseVersion(universe, "2025.2.1.0-b0");
    assert !XClusterUtil.supportsAutomaticDdl(universe)
        : "Universe should not support automatic DDL";
    TestHelper.updateUniverseVersion(universe, "2024.1.0.0-b100");
    assert !XClusterUtil.supportsAutomaticDdl(universe)
        : "Universe should not support automatic DDL";
  }

  private static final String MATVIEW_TABLE_ID = "000030af000030008000000000004009";

  private static MasterDdlOuterClass.ListTablesResponsePB.TableInfo buildTableInfo(
      String tableId, String tableName, RelationType relationType) {
    return MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder()
        .setTableType(CommonTypes.TableType.PGSQL_TABLE_TYPE)
        .setId(ByteString.copyFromUtf8(tableId))
        .setName(tableName)
        .setRelationType(relationType)
        .setNamespace(
            MasterTypes.NamespaceIdentifierPB.newBuilder()
                .setName("db1")
                .setId(ByteString.copyFromUtf8("00003000000030008000000000000000"))
                .build())
        .build();
  }

  private static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableListWithMatview() {
    return List.of(
        buildTableInfo(
            "000030af000030008000000000004000", "table_1", RelationType.USER_TABLE_RELATION),
        buildTableInfo(MATVIEW_TABLE_ID, "matview_1", RelationType.MATVIEW_TABLE_RELATION));
  }

  private static List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
      tableListWithoutMatview() {
    return List.of(
        buildTableInfo(
            "000030af000030008000000000004000", "table_1", RelationType.USER_TABLE_RELATION),
        buildTableInfo(
            "000030af000030008000000000004001", "index_1", RelationType.INDEX_TABLE_RELATION));
  }

  @Test
  public void testSupportsMatviewXClusterPreviewVersions() {
    Universe universe = ModelFactory.createUniverse("matview preview universe");

    // Right below the minimum preview version.
    TestHelper.updateUniverseVersion(universe, "2.29.0.0-b399");
    assertFalse(XClusterUtil.supportsMatviewXCluster(universe));

    // Exactly the minimum preview version.
    TestHelper.updateUniverseVersion(
        universe, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW);
    assertTrue(XClusterUtil.supportsMatviewXCluster(universe));

    // Newer versions in the same preview release line.
    TestHelper.updateUniverseVersion(universe, "2.29.0.0-b401");
    assertTrue(XClusterUtil.supportsMatviewXCluster(universe));
    TestHelper.updateUniverseVersion(universe, "2.29.1.0-b1");
    assertTrue(XClusterUtil.supportsMatviewXCluster(universe));

    // A newer preview release line.
    TestHelper.updateUniverseVersion(universe, "2.31.0.0-b1");
    assertTrue(XClusterUtil.supportsMatviewXCluster(universe));

    // An older preview release line, even with a much higher build number.
    TestHelper.updateUniverseVersion(universe, "2.27.0.0-b1000");
    assertFalse(XClusterUtil.supportsMatviewXCluster(universe));
  }

  @Test
  public void testSupportsMatviewXClusterStableVersions() {
    Universe universe = ModelFactory.createUniverse("matview stable universe");

    // Right below the minimum stable version.
    TestHelper.updateUniverseVersion(universe, "2025.2.1.0-b123");
    assertFalse(XClusterUtil.supportsMatviewXCluster(universe));

    // Exactly the minimum stable version.
    TestHelper.updateUniverseVersion(
        universe, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE);
    assertTrue(XClusterUtil.supportsMatviewXCluster(universe));

    // Newer versions in the same stable release line.
    TestHelper.updateUniverseVersion(universe, "2025.2.1.0-b125");
    assertTrue(XClusterUtil.supportsMatviewXCluster(universe));
    TestHelper.updateUniverseVersion(universe, "2025.2.2.0-b1");
    assertTrue(XClusterUtil.supportsMatviewXCluster(universe));

    // A newer stable release line.
    TestHelper.updateUniverseVersion(universe, "2026.1.0.0-b1");
    assertTrue(XClusterUtil.supportsMatviewXCluster(universe));

    // Older stable release lines, even with much higher build numbers.
    TestHelper.updateUniverseVersion(universe, "2025.1.5.0-b1000");
    assertFalse(XClusterUtil.supportsMatviewXCluster(universe));
    TestHelper.updateUniverseVersion(universe, "2024.2.0.0-b1000");
    assertFalse(XClusterUtil.supportsMatviewXCluster(universe));
  }

  @Test
  public void testSupportsMatviewXClusterOnUniverseInPreFinalizeState() {
    Universe universe = ModelFactory.createUniverse("matview pre finalize universe");
    TestHelper.updateUniverseVersion(
        universe, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE);
    PrevYBSoftwareConfig prevYBSoftwareConfig = new PrevYBSoftwareConfig();
    prevYBSoftwareConfig.setSoftwareVersion("2025.2.1.0-b123");
    prevYBSoftwareConfig.setAutoFlagConfigVersion(4);
    TestHelper.updateUniversePrevSoftwareConfig(universe, prevYBSoftwareConfig);
    TestHelper.updateUniverseSoftwareUpgradeState(universe, SoftwareUpgradeState.PreFinalize);

    // The upgrade is not finalized yet, so the previous version decides.
    assertFalse(XClusterUtil.supportsMatviewXCluster(universe));

    TestHelper.updateUniverseSoftwareUpgradeState(universe, SoftwareUpgradeState.Ready);
    assertTrue(XClusterUtil.supportsMatviewXCluster(universe));
  }

  @Test
  public void testIsMatviewReplicationSupportedMatrix() {
    Universe sourceUniverse = ModelFactory.createUniverse("matview matrix source Universe");
    Universe targetUniverse = ModelFactory.createUniverse("matview matrix target Universe");
    TestHelper.updateUniverseVersion(
        sourceUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW);
    TestHelper.updateUniverseVersion(
        targetUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW);

    // Automatic mode on a supported DB version.
    assertTrue(
        XClusterUtil.isMatviewReplicationSupported(
            true /* automaticDdlMode */, sourceUniverse, targetUniverse));
    // Semi-automatic and manual modes on a supported DB version.
    assertFalse(
        XClusterUtil.isMatviewReplicationSupported(
            false /* automaticDdlMode */, sourceUniverse, targetUniverse));

    // Automatic mode where only the source universe supports it.
    TestHelper.updateUniverseVersion(targetUniverse, "2.29.0.0-b399");
    assertFalse(
        XClusterUtil.isMatviewReplicationSupported(
            true /* automaticDdlMode */, sourceUniverse, targetUniverse));

    // Automatic mode where only the target universe supports it.
    TestHelper.updateUniverseVersion(sourceUniverse, "2.29.0.0-b399");
    TestHelper.updateUniverseVersion(
        targetUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW);
    assertFalse(
        XClusterUtil.isMatviewReplicationSupported(
            true /* automaticDdlMode */, sourceUniverse, targetUniverse));

    // Automatic mode where neither universe supports it.
    TestHelper.updateUniverseVersion(targetUniverse, "2.29.0.0-b399");
    assertFalse(
        XClusterUtil.isMatviewReplicationSupported(
            true /* automaticDdlMode */, sourceUniverse, targetUniverse));
  }

  @Test
  public void testIsMatviewReplicationSupportedFromConfig() {
    Universe sourceUniverse = ModelFactory.createUniverse("matview config source Universe");
    Universe targetUniverse = ModelFactory.createUniverse("matview config target Universe");
    TestHelper.updateUniverseVersion(
        sourceUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE);
    TestHelper.updateUniverseVersion(
        targetUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE);

    // Manual (basic) config.
    XClusterConfig basicConfig =
        XClusterConfig.create(
            "matview-basic",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            ConfigType.Basic);
    assertFalse(XClusterUtil.isMatviewReplicationSupported(basicConfig));

    // Semi-automatic (db scoped, no automatic DDL) config.
    XClusterConfig dbScopedConfig =
        XClusterConfig.create(
            "matview-db-scoped",
            sourceUniverse.getUniverseUUID(),
            targetUniverse.getUniverseUUID(),
            ConfigType.Db);
    // XClusterConfig.create only honors the Txn type, so set the db scoped type explicitly.
    dbScopedConfig.setType(ConfigType.Db);
    dbScopedConfig.update();
    assertFalse(XClusterUtil.isMatviewReplicationSupported(dbScopedConfig));

    // Automatic config.
    dbScopedConfig.setAutomaticDdlMode(true);
    dbScopedConfig.update();
    assertTrue(XClusterUtil.isMatviewReplicationSupported(dbScopedConfig));

    // Automatic config where the target universe does not support matview replication.
    TestHelper.updateUniverseVersion(targetUniverse, "2025.2.1.0-b123");
    assertFalse(XClusterUtil.isMatviewReplicationSupported(dbScopedConfig));

    // A config whose target universe no longer exists.
    TestHelper.updateUniverseVersion(
        targetUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE);
    dbScopedConfig.setTargetUniverseUUID(UUID.randomUUID());
    assertFalse(XClusterUtil.isMatviewReplicationSupported(dbScopedConfig));
  }

  @Test
  public void testCheckMatviewReplicationSupportedAutomaticMode() {
    Universe sourceUniverse = ModelFactory.createUniverse("matview check source Universe");
    Universe targetUniverse = ModelFactory.createUniverse("matview check target Universe");
    TestHelper.updateUniverseVersion(
        sourceUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW);
    TestHelper.updateUniverseVersion(
        targetUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW);

    // Supported: no exception.
    XClusterUtil.checkMatviewReplicationSupported(
        tableListWithMatview(), true /* automaticDdlMode */, sourceUniverse, targetUniverse);

    // The source universe is too old.
    TestHelper.updateUniverseVersion(sourceUniverse, "2.29.0.0-b399");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                XClusterUtil.checkMatviewReplicationSupported(
                    tableListWithMatview(),
                    true /* automaticDdlMode */,
                    sourceUniverse,
                    targetUniverse));
    assertTrue(
        exception.getMessage(),
        exception
            .getMessage()
            .contains(
                "Replicating materialized views is not supported in this version of the source"
                    + " universe"));
    assertTrue(exception.getMessage(), exception.getMessage().contains(MATVIEW_TABLE_ID));

    // The target universe is too old.
    TestHelper.updateUniverseVersion(
        sourceUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_PREVIEW);
    TestHelper.updateUniverseVersion(targetUniverse, "2.29.0.0-b399");
    exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                XClusterUtil.checkMatviewReplicationSupported(
                    tableListWithMatview(),
                    true /* automaticDdlMode */,
                    sourceUniverse,
                    targetUniverse));
    assertTrue(
        exception.getMessage(),
        exception
            .getMessage()
            .contains(
                "Replicating materialized views is not supported in this version of the target"
                    + " universe"));
  }

  @Test
  public void testCheckMatviewReplicationSupportedSemiAutomaticAndManualModes() {
    Universe sourceUniverse = ModelFactory.createUniverse("matview mode source Universe");
    Universe targetUniverse = ModelFactory.createUniverse("matview mode target Universe");
    // Both universes support replicating matviews, but the config is not in automatic DDL mode.
    TestHelper.updateUniverseVersion(
        sourceUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE);
    TestHelper.updateUniverseVersion(
        targetUniverse, XClusterUtil.MINIMUM_VERSION_MATVIEW_XCLUSTER_SUPPORT_STABLE);

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () ->
                XClusterUtil.checkMatviewReplicationSupported(
                    tableListWithMatview(),
                    false /* automaticDdlMode */,
                    sourceUniverse,
                    targetUniverse));
    assertTrue(
        exception.getMessage(),
        exception
            .getMessage()
            .contains(
                "Materialized views can be part of replication only for xCluster/DR configs in"
                    + " automatic DDL mode"));
    assertTrue(exception.getMessage(), exception.getMessage().contains(MATVIEW_TABLE_ID));
  }

  @Test
  public void testCheckMatviewReplicationSupportedWithoutMatviews() {
    Universe sourceUniverse = ModelFactory.createUniverse("matview none source Universe");
    Universe targetUniverse = ModelFactory.createUniverse("matview none target Universe");
    // Old versions on purpose; normal tables must keep working exactly as before.
    TestHelper.updateUniverseVersion(sourceUniverse, "2.29.0.0-b399");
    TestHelper.updateUniverseVersion(targetUniverse, "2.29.0.0-b399");

    for (boolean automaticDdlMode : new boolean[] {true, false}) {
      try {
        XClusterUtil.checkMatviewReplicationSupported(
            tableListWithoutMatview(), automaticDdlMode, sourceUniverse, targetUniverse);
      } catch (Exception e) {
        fail("Table lists without materialized views should never be rejected: " + e.getMessage());
      }
    }
  }
}
