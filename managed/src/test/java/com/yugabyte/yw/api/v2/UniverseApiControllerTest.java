// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.is;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.api.UniverseApi;
import com.yugabyte.yba.v2.client.models.UniverseCreateSpec;
import com.yugabyte.yba.v2.client.models.UniverseDeleteSpec;
import com.yugabyte.yba.v2.client.models.YBATask;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.DestroyUniverse;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;

/** Tests for Create/Get/Delete of Universe using v2.UniverseApiControllerImp */
@RunWith(JUnitParamsRunner.class)
public class UniverseApiControllerTest extends UniverseTestBase {

  @Test
  public void testGetUniverseV2() throws ApiException, IOException {
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe dbUniverse =
        Universe.saveDetails(
            uUUID,
            universe -> {
              // arch
              universe.getUniverseDetails().arch = PublicCloudConstants.Architecture.aarch64;
              // ysql
              universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL = true;
              universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQLAuth = true;
              universe.getUniverseDetails().getPrimaryCluster().userIntent.ysqlPassword =
                  "password#1";
              // regionList
              List<Region> regions = Region.getByProvider(providerUuid);
              universe.getUniverseDetails().getPrimaryCluster().userIntent.regionList =
                  regions != null
                      ? regions.stream().map(r -> r.getUuid()).toList()
                      : new ArrayList<>();
              // Volumes
              universe.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo =
                  ApiUtils.getDummyDeviceInfo(2, 150);
              // instanceTags
              universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceTags =
                  Map.of("tag1", "value1", "tag2", "value2");
              // instanceType
              universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType =
                  ApiUtils.UTIL_INST_TYPE;
              // GFlags
              universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      Map.of("mflag1", "mval1", "mflag2", "mval2"),
                      Map.of("tflag1", "tval1", "tflag2", "tval2"));
              SpecificGFlags.PerProcessFlags azFlags = new SpecificGFlags.PerProcessFlags();
              azFlags.value.put(ServerType.MASTER, Map.of("mperaz1", "val1", "mperaz2", "val2"));
              azFlags.value.put(ServerType.TSERVER, Map.of("tperaz1", "v1", "tperaz2", "v2"));
              universe
                  .getUniverseDetails()
                  .getPrimaryCluster()
                  .userIntent
                  .specificGFlags
                  .setPerAZ(
                      Map.of(universe.getUniverseDetails().getPrimaryCluster().uuid, azFlags));
            });
    when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
        .thenReturn(Optional.empty());
    UniverseApi api = new UniverseApi();
    com.yugabyte.yba.v2.client.models.Universe universeResp =
        api.getUniverse(customer.getUuid(), uUUID);
    validateUniverseSpec(universeResp.getSpec(), dbUniverse);
    validateUniverseInfo(universeResp.getInfo(), dbUniverse);
  }

  @Test
  public void testCreateUniverseV2() throws ApiException, IOException {
    UniverseApi api = new UniverseApi();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2();

    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);
    when(mockGFlagsValidation.getGFlagDetails(anyString(), anyString(), anyString()))
        .thenReturn(Optional.empty());
    YBATask createTask = api.createUniverse(customer.getUuid(), universeCreateSpec);
    assertThat(createTask.getTaskUuid(), is(fakeTaskUUID));
    ArgumentCaptor<UniverseDefinitionTaskParams> v1CreateParamsCapture =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), v1CreateParamsCapture.capture());
    UniverseDefinitionTaskParams v1CreateParams = v1CreateParamsCapture.getValue();

    // validate that the Universe create params matches properties specified in the createSpec
    validateUniverseCreateSpec(universeCreateSpec, v1CreateParams);
  }

  @Test
  public void testCreateUniverseWithRRV2() throws ApiException {
    UniverseApi api = new UniverseApi();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecWithRRV2();

    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);
    YBATask createTask = api.createUniverse(customer.getUuid(), universeCreateSpec);
    assertThat(createTask.getTaskUuid(), is(fakeTaskUUID));
    ArgumentCaptor<UniverseDefinitionTaskParams> v1CreateParamsCapture =
        ArgumentCaptor.forClass(UniverseDefinitionTaskParams.class);
    verify(mockCommissioner).submit(eq(TaskType.CreateUniverse), v1CreateParamsCapture.capture());
    UniverseDefinitionTaskParams v1CreateParams = v1CreateParamsCapture.getValue();

    // validate that the Universe create params matches properties specified in the createSpec
    validateUniverseCreateSpec(universeCreateSpec, v1CreateParams);
  }

  @Test
  public void testDeleteUniverseV2() throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(DestroyUniverse.Params.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());
    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);

    UniverseApi api = new UniverseApi();
    YBATask deleteTask = api.deleteUniverse(customer.getUuid(), u.getUniverseUUID(), null);
    UUID taskUUID = deleteTask.getTaskUuid();
    assertThat(taskUUID, is(fakeTaskUUID));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  @Parameters({
    "true, true, false",
    "false, true, true",
    "true, false, false",
    "false, false, true",
    "null, true, false",
  })
  public void testDeleteUniverseWithParamsV2(
      Boolean isForceDelete, Boolean isDeleteBackups, Boolean isDeleteAssociatedCerts)
      throws ApiException {
    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.DestroyUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(DestroyUniverse.Params.class)))
        .thenReturn(fakeTaskUUID);
    Universe u = createUniverse(customer.getId());
    // Add the cloud info into the universe.
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
          UniverseDefinitionTaskParams.UserIntent userIntent =
              new UniverseDefinitionTaskParams.UserIntent();
          userIntent.providerType = Common.CloudType.aws;
          universeDetails.upsertPrimaryCluster(userIntent, null);
          universe.setUniverseDetails(universeDetails);
        };
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), updater);

    UniverseDeleteSpec universeDeleteSpec = new UniverseDeleteSpec();
    universeDeleteSpec
        .isForceDelete(isForceDelete)
        .isDeleteBackups(isDeleteBackups)
        .isDeleteAssociatedCerts(isDeleteAssociatedCerts);
    UniverseApi api = new UniverseApi();
    YBATask deleteTask =
        api.deleteUniverse(customer.getUuid(), u.getUniverseUUID(), universeDeleteSpec);
    UUID taskUUID = deleteTask.getTaskUuid();
    assertThat(taskUUID, is(fakeTaskUUID));
    assertAuditEntry(1, customer.getUuid());
    ArgumentCaptor<DestroyUniverse.Params> destroyParams =
        ArgumentCaptor.forClass(DestroyUniverse.Params.class);
    verify(mockCommissioner).submit(eq(TaskType.DestroyUniverse), destroyParams.capture());
    assertThat(destroyParams.getValue().isForceDelete, is(isForceDelete));
    assertThat(destroyParams.getValue().isDeleteBackups, is(isDeleteBackups));
    assertThat(destroyParams.getValue().isDeleteAssociatedCerts, is(isDeleteAssociatedCerts));
  }
}
