package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import java.util.HashMap;
import java.util.Map;
import org.junit.Before;
import org.yb.client.YBClient;

public abstract class UniverseModifyBaseTest extends CommissionerBaseTest {
  protected static final String AZ_CODE = "az-1";

  protected Universe onPremUniverse;
  protected Universe defaultUniverse;

  protected ShellResponse dummyShellResponse;
  protected ShellResponse preflightResponse;

  protected YBClient mockClient;

  @Before
  public void setUp() {
    super.setUp();
    defaultUniverse = createUniverseForProvider("Test Universe", defaultProvider);
    onPremUniverse = createUniverseForProvider("Test onPrem Universe", onPremProvider);
    dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    preflightResponse = new ShellResponse();
    preflightResponse.message = "{\"test\": true}";
    when(mockNodeManager.nodeCommand(any(), any()))
        .then(
            invocation -> {
              if (invocation.getArgument(0).equals(NodeManager.NodeCommandType.Precheck)) {
                return preflightResponse;
              }
              return dummyShellResponse;
            });
    mockClient = mock(YBClient.class);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
  }

  protected Universe createUniverseForProvider(String universeName, Provider provider) {
    Region region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone zone = AvailabilityZone.createOrThrow(region, AZ_CODE, "AZ 1", "subnet-1");
    // create default universe
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.uuid);
    userIntent.instanceType = ApiUtils.UTIL_INST_TYPE;
    Common.CloudType providerType = Common.CloudType.valueOf(provider.code);
    userIntent.providerType = providerType;
    userIntent.provider = provider.uuid.toString();
    if (providerType == Common.CloudType.onprem) {
      createOnpremInstance(zone);
      createOnpremInstance(zone);
      createOnpremInstance(zone);
    }
    Map<String, String> gflags = new HashMap<>();
    gflags.put("foo", "bar");
    userIntent.masterGFlags = gflags;
    userIntent.tserverGFlags = gflags;
    Universe result = createUniverse(universeName, defaultCustomer.getCustomerId(), providerType);
    Universe.saveDetails(
        result.universeUUID, ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    return result;
  }

  protected void createOnpremInstance(AvailabilityZone zone) {
    NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
    nodeData.ip = "fake_ip_" + zone.region.code;
    nodeData.region = zone.region.code;
    nodeData.zone = zone.code;
    nodeData.instanceType = ApiUtils.UTIL_INST_TYPE;
    NodeInstance node = NodeInstance.create(zone.uuid, nodeData);
    node.save();
  }
}
