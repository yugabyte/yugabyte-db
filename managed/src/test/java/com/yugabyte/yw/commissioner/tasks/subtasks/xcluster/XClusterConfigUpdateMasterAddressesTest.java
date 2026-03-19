// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterTypes.MasterErrorPB;

@RunWith(MockitoJUnitRunner.class)
public class XClusterConfigUpdateMasterAddressesTest extends CommissionerBaseTest {

  private Universe sourceUniverse;
  private Universe targetUniverse;
  private XClusterConfig xClusterConfig;
  private YBClient mockClient;

  @Before
  public void setUp() throws Exception {
    defaultCustomer = testCustomer("XClusterConfigUpdateMasterAddresses-test-customer");

    UUID sourceUniverseUUID = UUID.randomUUID();
    sourceUniverse = createUniverse("source-universe", sourceUniverseUUID);
    UniverseDefinitionTaskParams sourceDetails = sourceUniverse.getUniverseDetails();
    NodeDetails sourceMaster = new NodeDetails();
    sourceMaster.isMaster = true;
    sourceMaster.isTserver = true;
    sourceMaster.state = NodeState.Live;
    sourceMaster.cloudInfo = new CloudSpecificInfo();
    sourceMaster.cloudInfo.private_ip = "10.0.0.1";
    sourceMaster.masterRpcPort = 7100;
    sourceMaster.placementUuid = sourceUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    sourceDetails.nodeDetailsSet.add(sourceMaster);
    sourceUniverse.setUniverseDetails(sourceDetails);
    sourceUniverse.update();

    targetUniverse = createUniverse("target-universe", UUID.randomUUID());

    XClusterConfigCreateFormData createFormData = new XClusterConfigCreateFormData();
    createFormData.name = "test-config";
    createFormData.sourceUniverseUUID = sourceUniverseUUID;
    createFormData.targetUniverseUUID = targetUniverse.getUniverseUUID();
    createFormData.tables = Collections.singleton("exampleTableId");
    xClusterConfig = XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    mockClient = mock(YBClient.class);
    lenient().when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
  }

  /**
   * Verifies that when getMasterClusterConfig returns an error response, the task does NOT throw an
   * exception. Before the change, the RuntimeException would propagate out of run(). After the
   * change, errors are caught and logged, and the background scheduler handles the update.
   */
  @Test
  public void testTaskDoesNotThrowWhenGetMasterClusterConfigFails() throws Exception {
    MasterErrorPB masterError =
        MasterErrorPB.newBuilder()
            .setCode(MasterErrorPB.Code.UNKNOWN_ERROR)
            .setStatus(
                AppStatusPB.newBuilder()
                    .setCode(ErrorCode.RUNTIME_ERROR)
                    .setMessage("simulated cluster config error"))
            .build();
    GetMasterClusterConfigResponse errorResponse =
        new GetMasterClusterConfigResponse(0, "", null, masterError);
    lenient().when(mockClient.getMasterClusterConfig()).thenReturn(errorResponse);

    XClusterConfigUpdateMasterAddresses task =
        AbstractTaskBase.createTask(XClusterConfigUpdateMasterAddresses.class);
    XClusterConfigUpdateMasterAddresses.Params params =
        new XClusterConfigUpdateMasterAddresses.Params();
    params.setUniverseUUID(targetUniverse.getUniverseUUID());
    params.sourceUniverseUuid = sourceUniverse.getUniverseUUID();
    params.xClusterConfig = xClusterConfig;
    task.initialize(params);

    // The RuntimeException from the inner getMasterClusterConfig error is caught and logged,
    // allowing the background syncMasterAddressesForConfig scheduler to handle the update.
    task.run();
  }
}
