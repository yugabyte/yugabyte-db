// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;

import com.yugabyte.yba.v2.client.ApiException;
import com.yugabyte.yba.v2.client.api.UniverseManagementApi;
import com.yugabyte.yba.v2.client.models.UniverseCreateSpec;
import com.yugabyte.yba.v2.client.models.UniverseResp;
import com.yugabyte.yba.v2.client.models.YBPTask;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Test;

public class UniverseManagementApiControllerImpTest extends UniverseManagementTestBase {

  @Test
  public void testGetUniverseV2() throws ApiException {
    UUID uUUID = createUniverse(customer.getId()).getUniverseUUID();
    Universe dbUniverse =
        Universe.saveDetails(
            uUUID,
            universe -> {
              // arch
              universe.getUniverseDetails().arch = PublicCloudConstants.Architecture.aarch64;
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
    UniverseManagementApi api = new UniverseManagementApi();
    UniverseResp universeResp = api.getUniverse(customer.getUuid(), uUUID);
    validateUniverseSpec(universeResp.getSpec(), dbUniverse);
    validateUniverseInfo(universeResp.getInfo(), dbUniverse);
  }

  @Test
  public void testCreateUniverseV2() throws ApiException {
    UniverseManagementApi api = new UniverseManagementApi();
    UniverseCreateSpec universeCreateSpec = getUniverseCreateSpecV2();

    UUID fakeTaskUUID = FakeDBApplication.buildTaskInfo(null, TaskType.CreateUniverse);
    when(mockCommissioner.submit(any(TaskType.class), any(UniverseDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    when(mockRuntimeConfig.getInt("yb.universe.otel_collector_metrics_port")).thenReturn(8889);
    YBPTask createTask = api.createUniverse(customer.getUuid(), universeCreateSpec);
    UUID universeUUID = createTask.getResourceUuid();
    Universe dbUniverse = Universe.getOrBadRequest(universeUUID, customer);

    // validate that the Universe created in the DB matches properties specified in the createSpec
    validateUniverseCreateSpec(universeCreateSpec, dbUniverse);

    // fetch the newly created Universe using v2 API and validate it once more
    UniverseResp universeResp = api.getUniverse(customer.getUuid(), universeUUID);
    validateUniverseSpec(universeResp.getSpec(), dbUniverse);
    validateUniverseInfo(universeResp.getInfo(), dbUniverse);
  }
}
