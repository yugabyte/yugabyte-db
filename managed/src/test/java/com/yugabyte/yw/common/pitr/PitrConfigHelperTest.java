// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.pitr;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.greaterThanOrEqualTo;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.TOO_MANY_REQUESTS;

import api.v2.utils.NormalizedPaginationSpec;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.services.config.YbClientConfig;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import io.ebean.PagedList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.yb.CommonTypes.TableType;
import org.yb.CommonTypes.YQLDatabase;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;

public class PitrConfigHelperTest extends FakeDBApplication {

  private static final long CACHE_TTL_MS = 60_000L;
  private static final int API_TIMEOUT_MS = 30_000;
  private static final int CACHE_MAX_UNIVERSES = 100;

  private Customer customer;
  private Universe universe;
  private Commissioner commissioner;
  private YBClientService mockYbClientService;
  private YbClientConfigFactory mockYbClientConfigFactory;
  private RuntimeConfGetter mockConfGetter;
  private YBClient mockYbClient;
  private ListSnapshotSchedulesResponse mockListSnapshotSchedulesResponse;
  private PitrConfigHelper helper;
  private ExecutorService executor;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.14.0.0-b111";
    details.softwareUpgradeState = SoftwareUpgradeState.Ready;
    details.universePaused = false;
    details.updateInProgress = false;
    universe.setUniverseDetails(details);
    universe.save();

    commissioner = app.injector().instanceOf(Commissioner.class);
    mockYbClientService = mock(YBClientService.class);
    mockYbClientConfigFactory = mock(YbClientConfigFactory.class);
    mockConfGetter = mock(RuntimeConfGetter.class);
    mockYbClient = mock(YBClient.class);
    YbClientConfig mockYbClientConfig = mock(YbClientConfig.class);
    mockListSnapshotSchedulesResponse = mock(ListSnapshotSchedulesResponse.class);

    stubRuntimeConf(CACHE_TTL_MS);
    lenient()
        .when(mockYbClientConfigFactory.create(any(), any(), anyLong()))
        .thenReturn(mockYbClientConfig);
    lenient()
        .when(mockYbClientService.getClientWithConfig(mockYbClientConfig))
        .thenReturn(mockYbClient);
    lenient().when(mockListSnapshotSchedulesResponse.hasError()).thenReturn(false);

    helper = newHelper();
    executor = Executors.newFixedThreadPool(4);
  }

  @After
  public void tearDown() {
    if (executor != null) {
      executor.shutdownNow();
    }
  }

  @Test
  public void listPitrConfigs_pausedUniverse_skipsMasterCall() throws Exception {
    universe.getUniverseDetails().universePaused = true;
    universe.save();
    UUID scheduleUuid = createPitrConfigInDb("paused-db").getUuid();

    List<PitrConfig> result = helper.listPitrConfigs(customer, universe);

    verify(mockYbClient, never()).listSnapshotSchedules(any());
    PitrConfig enriched = findByUuid(result, scheduleUuid);
    assertEquals(State.UNKNOWN, enriched.getState());
    assertEquals(enriched.getMinRecoverTimeInMillis(), enriched.getMaxRecoverTimeInMillis());
  }

  @Test
  public void listPitrConfigs_enrichesCompleteStateFromMaster() throws Exception {
    UUID scheduleUuid = createPitrConfigInDb("complete-db").getUuid();
    stubMasterSchedules(
        List.of(
            scheduleWithSnapshots(
                scheduleUuid, List.of(completeSnapshot(System.currentTimeMillis() - 3600_000L)))));

    List<PitrConfig> result = helper.listPitrConfigs(customer, universe);

    PitrConfig enriched = findByUuid(result, scheduleUuid);
    assertEquals(State.COMPLETE, enriched.getState());
    assertThat(enriched.getMinRecoverTimeInMillis(), greaterThanOrEqualTo(0L));
    assertThat(
        enriched.getMaxRecoverTimeInMillis(),
        greaterThanOrEqualTo(enriched.getMinRecoverTimeInMillis()));
    verifyMasterListCalledOnce();
  }

  @Test
  public void listPitrConfigs_enrichesFailedStateWhenSnapshotFailed() throws Exception {
    UUID scheduleUuid = createPitrConfigInDb("failed-db").getUuid();
    stubMasterSchedules(
        List.of(
            scheduleWithSnapshots(
                scheduleUuid,
                List.of(
                    new SnapshotInfo(
                        UUID.randomUUID(),
                        System.currentTimeMillis(),
                        System.currentTimeMillis() - 1000L,
                        State.FAILED)))));

    List<PitrConfig> result = helper.listPitrConfigs(customer, universe);

    assertEquals(State.FAILED, findByUuid(result, scheduleUuid).getState());
  }

  @Test
  public void listPitrConfigs_dbRowMissingFromMaster_returnsUnknown() throws Exception {
    UUID scheduleUuid = createPitrConfigInDb("orphan-db").getUuid();
    stubMasterSchedules(List.of());

    List<PitrConfig> result = helper.listPitrConfigs(customer, universe);

    assertEquals(State.UNKNOWN, findByUuid(result, scheduleUuid).getState());
  }

  @Test
  public void listPitrConfigs_masterErrorResponse_returnsUnknown() throws Exception {
    UUID scheduleUuid = createPitrConfigInDb("master-error-db").getUuid();
    when(mockListSnapshotSchedulesResponse.hasError()).thenReturn(true);
    when(mockYbClient.listSnapshotSchedules(isNull()))
        .thenReturn(mockListSnapshotSchedulesResponse);

    List<PitrConfig> result = helper.listPitrConfigs(customer, universe);

    assertEquals(State.UNKNOWN, findByUuid(result, scheduleUuid).getState());
  }

  @Test
  public void listPitrConfigs_masterThrows_returnsInternalError() throws Exception {
    createPitrConfigInDb("throws-db");
    when(mockYbClient.listSnapshotSchedules(isNull()))
        .thenThrow(new RuntimeException("master down"));

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class, () -> helper.listPitrConfigs(customer, universe));

    assertEquals(INTERNAL_SERVER_ERROR, ex.getHttpStatus());
  }

  @Test
  public void listPitrConfigs_cacheHit_callsMasterOnlyOnce() throws Exception {
    UUID scheduleUuid = createPitrConfigInDb("cached-db").getUuid();
    stubMasterSchedules(
        List.of(
            scheduleWithSnapshots(
                scheduleUuid, List.of(completeSnapshot(System.currentTimeMillis())))));

    helper.listPitrConfigs(customer, universe);
    helper.listPitrConfigs(customer, universe);

    verifyMasterListCalledOnce();
  }

  @Test
  public void listPitrConfigs_cacheExpires_refetchesFromMaster() throws Exception {
    helper = newHelperWithCacheTtl(50L);
    UUID scheduleUuid = createPitrConfigInDb("expiry-db").getUuid();
    stubMasterSchedules(
        List.of(
            scheduleWithSnapshots(
                scheduleUuid, List.of(completeSnapshot(System.currentTimeMillis())))));

    helper.listPitrConfigs(customer, universe);
    Thread.sleep(100);
    helper.listPitrConfigs(customer, universe);

    verify(mockYbClient, times(2)).listSnapshotSchedules(isNull());
  }

  @Test
  public void listPitrConfigs_concurrentRefreshInProgress_returnsTooManyRequests()
      throws Exception {
    createPitrConfigInDb("throttle-db");
    CountDownLatch loadStarted = new CountDownLatch(1);
    CountDownLatch allowLoadToFinish = new CountDownLatch(1);
    when(mockYbClient.listSnapshotSchedules(isNull()))
        .thenAnswer(
            invocation -> {
              loadStarted.countDown();
              allowLoadToFinish.await(5, TimeUnit.SECONDS);
              return mockListSnapshotSchedulesResponse;
            });
    when(mockListSnapshotSchedulesResponse.getSnapshotScheduleInfoList()).thenReturn(List.of());

    Future<?> firstList =
        executor.submit(
            () -> {
              try {
                helper.listPitrConfigs(customer, universe);
              } catch (Exception e) {
                throw new RuntimeException(e);
              }
            });
    loadStarted.await(5, TimeUnit.SECONDS);

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class, () -> helper.listPitrConfigs(customer, universe));

    assertEquals(TOO_MANY_REQUESTS, ex.getHttpStatus());
    allowLoadToFinish.countDown();
    firstList.get(10, TimeUnit.SECONDS);
  }

  @Test
  public void listPitrConfigs_concurrentAfterRefreshUsesCacheWithout429() throws Exception {
    createPitrConfigInDb("post-refresh-db");
    CountDownLatch loadStarted = new CountDownLatch(1);
    CountDownLatch allowLoadToFinish = new CountDownLatch(1);
    when(mockYbClient.listSnapshotSchedules(isNull()))
        .thenAnswer(
            invocation -> {
              loadStarted.countDown();
              allowLoadToFinish.await(5, TimeUnit.SECONDS);
              return mockListSnapshotSchedulesResponse;
            });
    when(mockListSnapshotSchedulesResponse.getSnapshotScheduleInfoList()).thenReturn(List.of());

    Future<?> firstList =
        executor.submit(
            () -> {
              try {
                helper.listPitrConfigs(customer, universe);
              } catch (Exception e) {
                throw new RuntimeException(e);
              }
            });
    loadStarted.await(5, TimeUnit.SECONDS);
    allowLoadToFinish.countDown();
    firstList.get(10, TimeUnit.SECONDS);

    List<PitrConfig> secondList = helper.listPitrConfigs(customer, universe);

    assertEquals(1, secondList.size());
    verifyMasterListCalledOnce();
  }

  @Test
  public void listPitrConfigs_usesConfiguredApiTimeout() throws Exception {
    createPitrConfigInDb("timeout-db");
    stubMasterSchedules(List.of());

    helper.listPitrConfigs(customer, universe);

    ArgumentCaptor<Long> timeoutCaptor = ArgumentCaptor.forClass(Long.class);
    verify(mockYbClientConfigFactory).create(any(), any(), timeoutCaptor.capture());
    assertEquals(API_TIMEOUT_MS, timeoutCaptor.getValue().longValue());
  }

  @Test
  public void listPitrConfigs_incompatibleDbVersion_throwsBadRequest() {
    universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
        "2.12.0.0-b111";
    universe.save();

    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class, () -> helper.listPitrConfigs(customer, universe));

    assertEquals(BAD_REQUEST, ex.getHttpStatus());
  }

  @Test
  public void listPitrConfigs_wrongCustomer_throwsBadRequest() {
    Customer otherCustomer = ModelFactory.testCustomer("other");

    assertThrows(
        PlatformServiceException.class, () -> helper.listPitrConfigs(otherCustomer, universe));
  }

  @Test
  public void listPitrConfigsPaged_invalidDrConfigUuid_throws() {
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                helper.listPitrConfigsPaged(
                    universe, UUID.randomUUID(), new NormalizedPaginationSpec(0, 10, "asc")));

    assertEquals(NOT_FOUND, ex.getHttpStatus());
  }

  @Test
  public void listPitrConfigsPaged_returnsEnrichedPagedRows() throws Exception {
    UUID scheduleUuid = createPitrConfigInDb("paged-db").getUuid();
    stubMasterSchedules(
        List.of(
            scheduleWithSnapshots(
                scheduleUuid, List.of(completeSnapshot(System.currentTimeMillis())))));

    PagedList<PitrConfig> paged =
        helper.listPitrConfigsPaged(universe, new NormalizedPaginationSpec(0, 10, "asc"));

    assertThat(paged.getTotalCount(), greaterThanOrEqualTo(1));
    assertEquals(State.COMPLETE, findByUuid(paged.getList(), scheduleUuid).getState());
    verifyMasterListCalledOnce();
  }

  @Test
  public void listPitrConfigs_emptyList_skipsMasterCall() throws Exception {
    Universe emptyUniverse = ModelFactory.createUniverse("empty-pitr-universe", customer.getId());
    UniverseDefinitionTaskParams emptyDetails = emptyUniverse.getUniverseDetails();
    emptyDetails.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.14.0.0-b111";
    emptyDetails.softwareUpgradeState = SoftwareUpgradeState.Ready;
    emptyUniverse.setUniverseDetails(emptyDetails);
    emptyUniverse.save();

    List<PitrConfig> result = helper.listPitrConfigs(customer, emptyUniverse);

    assertEquals(0, result.size());
    verify(mockYbClient, times(0)).listSnapshotSchedules(any());
  }

  private PitrConfigHelper newHelper() {
    return newHelperWithCacheTtl(CACHE_TTL_MS);
  }

  private PitrConfigHelper newHelperWithCacheTtl(long cacheTtlMs) {
    stubRuntimeConf(cacheTtlMs);
    return new PitrConfigHelper(
        commissioner, mockYbClientService, mockYbClientConfigFactory, mockConfGetter);
  }

  private void stubRuntimeConf(long cacheTtlMs) {
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.pitrListSnapshotSchedulesCacheTtlMs))
        .thenReturn((int) cacheTtlMs);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.pitrListSnapshotSchedulesCacheMaxUniverses))
        .thenReturn(CACHE_MAX_UNIVERSES);
    when(mockConfGetter.getGlobalConf(GlobalConfKeys.pitrListApiTimeoutMs))
        .thenReturn(API_TIMEOUT_MS);
  }

  private PitrConfig createPitrConfigInDb(String dbName) {
    UUID scheduleUuid = UUID.randomUUID();
    CreatePitrConfigParams params = new CreatePitrConfigParams();
    params.retentionPeriodInSeconds = 7 * 86400L;
    params.intervalInSeconds = 86400L;
    params.setUniverseUUID(universe.getUniverseUUID());
    params.customerUUID = customer.getUuid();
    params.keyspaceName = dbName;
    params.tableType = TableType.PGSQL_TABLE_TYPE;
    return PitrConfig.create(scheduleUuid, params);
  }

  private void stubMasterSchedules(List<SnapshotScheduleInfo> schedules) throws Exception {
    when(mockListSnapshotSchedulesResponse.getSnapshotScheduleInfoList()).thenReturn(schedules);
    when(mockYbClient.listSnapshotSchedules(isNull()))
        .thenReturn(mockListSnapshotSchedulesResponse);
  }

  private SnapshotScheduleInfo scheduleWithSnapshots(
      UUID scheduleUuid, List<SnapshotInfo> snapshots) {
    return new SnapshotScheduleInfo(
        scheduleUuid, 86400L, 7L * 86400L, snapshots, null, null, YQLDatabase.YQL_DATABASE_PGSQL);
  }

  private SnapshotInfo completeSnapshot(long completionTime) {
    return new SnapshotInfo(
        UUID.randomUUID(), completionTime + 1000L, completionTime, State.COMPLETE);
  }

  private void verifyMasterListCalledOnce() throws Exception {
    verify(mockYbClient, times(1)).listSnapshotSchedules(isNull());
  }

  private PitrConfig findByUuid(List<PitrConfig> configs, UUID uuid) {
    return configs.stream()
        .filter(config -> config.getUuid().equals(uuid))
        .findFirst()
        .orElseThrow(() -> new AssertionError("Missing PITR config " + uuid));
  }
}
