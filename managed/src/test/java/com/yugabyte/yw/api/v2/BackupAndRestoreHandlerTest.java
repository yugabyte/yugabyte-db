// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.NOT_FOUND;

import api.v2.handlers.BackupAndRestoreHandler;
import api.v2.models.BackupPagedQuerySpec;
import api.v2.models.BackupPagedResp;
import api.v2.models.PaginationSpec;
import api.v2.models.Restore;
import api.v2.models.RestoreApiFilter;
import api.v2.models.RestoreKeyspaceInfo;
import api.v2.models.RestoreKeyspacePagedQuerySpec;
import api.v2.models.RestoreKeyspacePagedResp;
import api.v2.models.RestorePagedQuerySpec;
import api.v2.models.RestorePagedResp;
import api.v2.models.RestoreState;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.RestoreKeyspace;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.filters.RestoreFilter;
import com.yugabyte.yw.models.paging.RestorePagedQuery;
import com.yugabyte.yw.models.paging.RestorePagedResponse;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
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

  @Test
  public void pageListRestores_returnsPagedRows() {
    com.yugabyte.yw.models.Restore restore = createRestore(universe);

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);

    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertThat(resp.getTotalCount(), greaterThanOrEqualTo(1));
    assertThat(resp.getEntities().size(), greaterThanOrEqualTo(1));
    Restore entity = resp.getEntities().get(0);
    assertEquals(restore.getRestoreUUID(), entity.getInfo().getUuid());
    assertEquals(RestoreState.InProgress, entity.getInfo().getState());
    assertEquals(universe.getUniverseUUID(), entity.getInfo().getUniverseUuid());
    assertEquals(universe.getName(), entity.getInfo().getUniverseName());
    assertEquals(customer.getUuid(), entity.getInfo().getCustomerUuid());
  }

  @Test
  public void pageListRestores_filtersByState() {
    com.yugabyte.yw.models.Restore restore = createRestore(universe);

    RestorePagedQuerySpec matchingSpec = new RestorePagedQuerySpec();
    matchingSpec.offset(0).limit(10);
    matchingSpec.setFilter(new RestoreApiFilter().states(List.of(RestoreState.InProgress)));
    RestorePagedResp matching = handler.pageListRestores(customer.getUuid(), matchingSpec);
    assertTrue(
        matching.getEntities().stream()
            .anyMatch(r -> restore.getRestoreUUID().equals(r.getInfo().getUuid())));

    RestorePagedQuerySpec nonMatchingSpec = new RestorePagedQuerySpec();
    nonMatchingSpec.offset(0).limit(10);
    nonMatchingSpec.setFilter(new RestoreApiFilter().states(List.of(RestoreState.Completed)));
    RestorePagedResp nonMatching = handler.pageListRestores(customer.getUuid(), nonMatchingSpec);
    assertTrue(
        nonMatching.getEntities().stream()
            .noneMatch(r -> restore.getRestoreUUID().equals(r.getInfo().getUuid())));
  }

  @Test
  public void pageListRestores_invalidCustomer() {
    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);

    PlatformServiceException pse =
        assertThrows(
            PlatformServiceException.class,
            () -> handler.pageListRestores(UUID.randomUUID(), spec));
    assertEquals(NOT_FOUND, pse.getHttpStatus());
  }

  @Test
  public void pageListRestores_paginatesWithTotalCountAndHasNext() {
    createRestore(universe);
    createRestore(universe);
    createRestore(universe);

    RestorePagedQuerySpec firstPage = new RestorePagedQuerySpec();
    firstPage.offset(0).limit(2);
    RestorePagedResp page1 = handler.pageListRestores(customer.getUuid(), firstPage);
    assertEquals(3, page1.getTotalCount());
    assertEquals(2, page1.getEntities().size());
    assertTrue(page1.getHasNext());

    RestorePagedQuerySpec secondPage = new RestorePagedQuerySpec();
    secondPage.offset(2).limit(2);
    RestorePagedResp page2 = handler.pageListRestores(customer.getUuid(), secondPage);
    assertEquals(3, page2.getTotalCount());
    assertEquals(1, page2.getEntities().size());
    assertFalse(page2.getHasNext());
  }

  @Test
  public void pageListRestores_isolatedByCustomer() {
    com.yugabyte.yw.models.Restore mine = createRestore(universe);

    Customer otherCustomer = ModelFactory.testCustomer("tc2", "other@customer.com");
    Universe otherUniverse = ModelFactory.createUniverse(otherCustomer.getId());
    createRestoreFor(otherCustomer, otherUniverse);

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);
    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(mine.getRestoreUUID(), resp.getEntities().get(0).getInfo().getUuid());
  }

  @Test
  public void pageListRestores_filtersByUniverseUuid() {
    com.yugabyte.yw.models.Restore onTarget = createRestore(universe);
    Universe otherUniverse = ModelFactory.createUniverse("other-universe", customer.getId());
    createRestore(otherUniverse);

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);
    spec.setFilter(new RestoreApiFilter().universeUuidList(List.of(universe.getUniverseUUID())));
    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(onTarget.getRestoreUUID(), resp.getEntities().get(0).getInfo().getUuid());
  }

  @Test
  public void pageListRestores_filtersByRestoreUuid() {
    com.yugabyte.yw.models.Restore wanted = createRestore(universe);
    createRestore(universe);

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);
    spec.setFilter(new RestoreApiFilter().restoreUuidList(List.of(wanted.getRestoreUUID())));
    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(wanted.getRestoreUUID(), resp.getEntities().get(0).getInfo().getUuid());
  }

  @Test
  public void pageListRestores_filtersByStorageConfigUuid() {
    com.yugabyte.yw.models.Restore wanted = createRestore(universe);
    createRestore(universe);

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);
    spec.setFilter(
        new RestoreApiFilter().storageConfigUuidList(List.of(wanted.getStorageConfigUUID())));
    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(wanted.getRestoreUUID(), resp.getEntities().get(0).getInfo().getUuid());
  }

  @Test
  public void pageListRestores_filtersBySourceUniverseName() {
    com.yugabyte.yw.models.Restore wanted = createRestore(universe);
    wanted.setSourceUniverseName("prod-source-universe");
    wanted.save();
    com.yugabyte.yw.models.Restore other = createRestore(universe);
    other.setSourceUniverseName("dev-source-universe");
    other.save();

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);
    spec.setFilter(new RestoreApiFilter().sourceUniverseNameList(List.of("prod")));
    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(wanted.getRestoreUUID(), resp.getEntities().get(0).getInfo().getUuid());
  }

  @Test
  public void pageListRestores_filtersByMultipleStates() {
    com.yugabyte.yw.models.Restore inProgress = createRestore(universe);
    com.yugabyte.yw.models.Restore completed = createRestore(universe);
    completed.setState(com.yugabyte.yw.models.Restore.State.Completed);
    completed.save();
    com.yugabyte.yw.models.Restore failed = createRestore(universe);
    failed.setState(com.yugabyte.yw.models.Restore.State.Failed);
    failed.save();

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);
    spec.setFilter(
        new RestoreApiFilter().states(List.of(RestoreState.InProgress, RestoreState.Completed)));
    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertEquals(2, resp.getTotalCount());
    List<UUID> ids = resp.getEntities().stream().map(r -> r.getInfo().getUuid()).toList();
    assertTrue(ids.contains(inProgress.getRestoreUUID()));
    assertTrue(ids.contains(completed.getRestoreUUID()));
    assertFalse(ids.contains(failed.getRestoreUUID()));
  }

  @Test
  public void pageListRestores_combinedStateAndUniverseFilters() {
    // Matches both predicates.
    com.yugabyte.yw.models.Restore match = createRestore(universe);
    match.setState(com.yugabyte.yw.models.Restore.State.Completed);
    match.save();
    // Right universe, wrong state.
    com.yugabyte.yw.models.Restore wrongState = createRestore(universe);
    wrongState.setState(com.yugabyte.yw.models.Restore.State.Failed);
    wrongState.save();
    // Right state, wrong universe.
    Universe otherUniverse = ModelFactory.createUniverse("combined-other", customer.getId());
    com.yugabyte.yw.models.Restore wrongUniverse = createRestore(otherUniverse);
    wrongUniverse.setState(com.yugabyte.yw.models.Restore.State.Completed);
    wrongUniverse.save();

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);
    spec.setFilter(
        new RestoreApiFilter()
            .states(List.of(RestoreState.Completed))
            .universeUuidList(List.of(universe.getUniverseUUID())));
    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(match.getRestoreUUID(), resp.getEntities().get(0).getInfo().getUuid());
  }

  @Test
  public void pageListRestores_filtersByDateRange() {
    com.yugabyte.yw.models.Restore inWindow = createRestore(universe);
    inWindow.setCreateTime(new Date(2_000_000L));
    inWindow.save();
    com.yugabyte.yw.models.Restore tooOld = createRestore(universe);
    tooOld.setCreateTime(new Date(500L));
    tooOld.save();
    com.yugabyte.yw.models.Restore tooNew = createRestore(universe);
    tooNew.setCreateTime(new Date(9_000_000L));
    tooNew.save();

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);
    spec.setFilter(
        new RestoreApiFilter()
            .dateRangeStart(new Date(1_000_000L).toInstant().atOffset(java.time.ZoneOffset.UTC))
            .dateRangeEnd(new Date(3_000_000L).toInstant().atOffset(java.time.ZoneOffset.UTC)));
    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(inWindow.getRestoreUUID(), resp.getEntities().get(0).getInfo().getUuid());
  }

  @Test
  public void pageListRestores_excludesHiddenByDefaultAndIncludesWhenRequested() {
    com.yugabyte.yw.models.Restore visible = createRestore(universe);
    com.yugabyte.yw.models.Restore hidden = createRestore(universe);
    hidden.setHidden(true);
    hidden.save();

    RestorePagedQuerySpec defaultSpec = new RestorePagedQuerySpec();
    defaultSpec.offset(0).limit(10);
    RestorePagedResp defaultResp = handler.pageListRestores(customer.getUuid(), defaultSpec);
    assertEquals(1, defaultResp.getTotalCount());
    assertEquals(visible.getRestoreUUID(), defaultResp.getEntities().get(0).getInfo().getUuid());

    RestorePagedQuerySpec showHiddenSpec = new RestorePagedQuerySpec();
    showHiddenSpec.offset(0).limit(10);
    showHiddenSpec.setFilter(new RestoreApiFilter().showHidden(true));
    RestorePagedResp showHiddenResp = handler.pageListRestores(customer.getUuid(), showHiddenSpec);
    assertEquals(2, showHiddenResp.getTotalCount());
  }

  @Test
  public void pageListRestores_ordersByCreateTimeDirection() {
    com.yugabyte.yw.models.Restore older = createRestore(universe);
    older.setCreateTime(new Date(1_000L));
    older.save();
    com.yugabyte.yw.models.Restore newer = createRestore(universe);
    newer.setCreateTime(new Date(2_000L));
    newer.save();

    RestorePagedQuerySpec descSpec = new RestorePagedQuerySpec();
    descSpec.offset(0).limit(10).direction(PaginationSpec.DirectionEnum.DESC);
    List<UUID> descIds =
        handler.pageListRestores(customer.getUuid(), descSpec).getEntities().stream()
            .map(r -> r.getInfo().getUuid())
            .collect(Collectors.toList());
    assertThat(descIds, contains(newer.getRestoreUUID(), older.getRestoreUUID()));

    RestorePagedQuerySpec ascSpec = new RestorePagedQuerySpec();
    ascSpec.offset(0).limit(10).direction(PaginationSpec.DirectionEnum.ASC);
    List<UUID> ascIds =
        handler.pageListRestores(customer.getUuid(), ascSpec).getEntities().stream()
            .map(r -> r.getInfo().getUuid())
            .collect(Collectors.toList());
    assertThat(ascIds, contains(older.getRestoreUUID(), newer.getRestoreUUID()));
  }

  @Test
  public void pageListRestores_emptyFilterReturnsAll() {
    createRestore(universe);
    createRestore(universe);

    RestorePagedQuerySpec spec = new RestorePagedQuerySpec();
    spec.offset(0).limit(10);
    spec.setFilter(new RestoreApiFilter());
    RestorePagedResp resp = handler.pageListRestores(customer.getUuid(), spec);

    assertEquals(2, resp.getTotalCount());
  }

  @Test
  public void pagedListRestores_sharedEntryPoint_returnsRawEntitiesWithDefaultSort() {
    com.yugabyte.yw.models.Restore older = createRestore(universe);
    older.setCreateTime(new Date(1_000L));
    older.save();
    com.yugabyte.yw.models.Restore newer = createRestore(universe);
    newer.setCreateTime(new Date(2_000L));
    newer.save();

    RestoreFilter filter = RestoreFilter.builder().customerUUID(customer.getUuid()).build();
    RestorePagedQuery query = new RestorePagedQuery();
    query.setFilter(filter);
    query.setOffset(0);
    query.setLimit(10);
    query.setNeedTotalCount(true);

    RestorePagedResponse response = handler.pagedListRestores(query);

    assertEquals(2, (int) response.getTotalCount());
    List<UUID> ids =
        response.getEntities().stream()
            .map(com.yugabyte.yw.models.Restore::getRestoreUUID)
            .collect(Collectors.toList());
    assertThat(ids, contains(newer.getRestoreUUID(), older.getRestoreUUID()));
  }

  @Test
  public void pageListRestoreKeyspaces_returnsPagedRows() {
    com.yugabyte.yw.models.Restore restore = createRestore(universe);
    createRestoreKeyspace(restore, "db1");
    createRestoreKeyspace(restore, "db2");

    RestoreKeyspacePagedQuerySpec spec = new RestoreKeyspacePagedQuerySpec();
    spec.offset(0).limit(1);

    RestoreKeyspacePagedResp resp =
        handler.pageListRestoreKeyspaces(customer.getUuid(), restore.getRestoreUUID(), spec);

    assertEquals(2, resp.getTotalCount());
    assertEquals(1, resp.getEntities().size());
    assertTrue(resp.getHasNext());

    RestoreKeyspaceInfo entity = resp.getEntities().get(0);
    assertEquals(restore.getRestoreUUID(), entity.getRestoreUuid());
    assertEquals(RestoreState.InProgress, entity.getState());
  }

  @Test
  public void pageListRestoreKeyspaces_isolatedByRestore() {
    com.yugabyte.yw.models.Restore restoreA = createRestore(universe);
    com.yugabyte.yw.models.Restore restoreB = createRestore(universe);
    RestoreKeyspace ksA = createRestoreKeyspace(restoreA, "db-a");
    createRestoreKeyspace(restoreB, "db-b");

    RestoreKeyspacePagedQuerySpec spec = new RestoreKeyspacePagedQuerySpec();
    spec.offset(0).limit(10);

    RestoreKeyspacePagedResp resp =
        handler.pageListRestoreKeyspaces(customer.getUuid(), restoreA.getRestoreUUID(), spec);

    assertEquals(1, resp.getTotalCount());
    assertEquals(ksA.getUuid(), resp.getEntities().get(0).getUuid());
  }

  @Test
  public void pageListRestoreKeyspaces_invalidRestore() {
    RestoreKeyspacePagedQuerySpec spec = new RestoreKeyspacePagedQuerySpec();
    spec.offset(0).limit(10);

    assertThrows(
        PlatformServiceException.class,
        () -> handler.pageListRestoreKeyspaces(customer.getUuid(), UUID.randomUUID(), spec));
  }

  private com.yugabyte.yw.models.Restore createRestore(Universe targetUniverse) {
    return createRestoreFor(customer, targetUniverse);
  }

  private com.yugabyte.yw.models.Restore createRestoreFor(Customer owner, Universe targetUniverse) {
    CustomerConfig customerConfig =
        ModelFactory.createS3StorageConfig(owner, "restore-storage-" + UUID.randomUUID());

    BackupTableParams backupParams = new BackupTableParams();
    backupParams.setUniverseUUID(targetUniverse.getUniverseUUID());
    backupParams.storageConfigUUID = customerConfig.getConfigUUID();
    backupParams.customerUuid = owner.getUuid();
    Backup backup = Backup.create(owner.getUuid(), backupParams);
    backup.setTaskUUID(UUID.randomUUID());
    backup.save();

    RestoreBackupParams restoreParams = new RestoreBackupParams();
    restoreParams.prefixUUID = UUID.randomUUID();
    restoreParams.customerUUID = owner.getUuid();
    restoreParams.setUniverseUUID(targetUniverse.getUniverseUUID());
    restoreParams.storageConfigUUID = customerConfig.getConfigUUID();
    restoreParams.backupStorageInfoList = new ArrayList<>();
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.storageLocation = backup.getBackupInfo().storageLocation;
    storageInfo.keyspace = backup.getBackupInfo().getKeyspace();
    restoreParams.backupStorageInfoList.add(storageInfo);

    return com.yugabyte.yw.models.Restore.create(UUID.randomUUID(), restoreParams);
  }

  private RestoreKeyspace createRestoreKeyspace(
      com.yugabyte.yw.models.Restore restore, String keyspace) {
    RestoreBackupParams params = new RestoreBackupParams();
    params.prefixUUID = restore.getRestoreUUID();
    params.customerUUID = customer.getUuid();
    params.setUniverseUUID(restore.getUniverseUUID());
    params.backupStorageInfoList = new ArrayList<>();
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.keyspace = keyspace;
    storageInfo.storageLocation = "s3://bucket/univ/keyspace-" + keyspace;
    params.backupStorageInfoList.add(storageInfo);
    return RestoreKeyspace.create(UUID.randomUUID(), params);
  }
}
