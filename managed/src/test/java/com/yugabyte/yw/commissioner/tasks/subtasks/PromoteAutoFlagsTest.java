// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterClusterOuterClass.PromoteAutoFlagsResponsePB;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;

@RunWith(MockitoJUnitRunner.class)
public class PromoteAutoFlagsTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private YBClient mockClient;
  private NodeDetails node;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    node = new NodeDetails();
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "1.2.3.4";
    node.nodeName = "node-1";
    details.nodeDetailsSet.add(node);
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();
    mockClient = mock(YBClient.class);
    try {
      // TODO: set non-zero sleep time by mocking parent task.
      RuntimeConfigEntry.upsertGlobal("yb.upgrade.auto_flag_update_sleep_time_ms", "0");
      lenient().when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    } catch (Exception ignored) {
      fail();
    }
  }

  @Test
  public void testPromoteAutoFlagException() throws Exception {
    PromoteAutoFlags.Params params = new PromoteAutoFlags.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.maxClass = AutoFlagUtil.EXTERNAL_AUTO_FLAG_CLASS_NAME;
    when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
        .thenThrow(new Exception("Error promoting auto flags"));
    PromoteAutoFlags task = AbstractTaskBase.createTask(PromoteAutoFlags.class);
    task.initialize(params);
    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(INTERNAL_SERVER_ERROR, exception.getHttpStatus());
    assertEquals("Error promoting auto flags", exception.getMessage());
  }

  @Test
  public void testPromoteAutoFlagFail() throws Exception {
    PromoteAutoFlags.Params params = new PromoteAutoFlags.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.maxClass = AutoFlagUtil.EXTERNAL_AUTO_FLAG_CLASS_NAME;
    when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
        .thenReturn(
            new PromoteAutoFlagsResponse(
                0,
                "uuid",
                PromoteAutoFlagsResponsePB.newBuilder()
                    .setNewConfigVersion(1)
                    .setNonRuntimeFlagsPromoted(false)
                    .setError(
                        MasterErrorPB.newBuilder()
                            .setStatus(
                                AppStatusPB.newBuilder()
                                    .setCode(ErrorCode.CONFIGURATION_ERROR)
                                    .build())
                            .setCode(Code.UNKNOWN_ERROR)
                            .build())
                    .build()))
        .thenReturn(
            new PromoteAutoFlagsResponse(
                0,
                "uuid",
                PromoteAutoFlagsResponsePB.newBuilder()
                    .setNewConfigVersion(1)
                    .setNonRuntimeFlagsPromoted(false)
                    .setError(
                        MasterErrorPB.newBuilder()
                            .setStatus(
                                AppStatusPB.newBuilder().setCode(ErrorCode.ALREADY_PRESENT).build())
                            .setCode(Code.UNKNOWN_ERROR)
                            .build())
                    .build()));
    PromoteAutoFlags task = AbstractTaskBase.createTask(PromoteAutoFlags.class);
    task.initialize(params);
    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(INTERNAL_SERVER_ERROR, exception.getHttpStatus());
    task.run();
  }

  @Test
  public void testPromoteAutoFlagSuccess() throws Exception {
    PromoteAutoFlags.Params params = new PromoteAutoFlags.Params();
    params.maxClass = AutoFlagUtil.EXTERNAL_AUTO_FLAG_CLASS_NAME;
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
        .thenReturn(
            new PromoteAutoFlagsResponse(
                0, "uuid", PromoteAutoFlagsResponsePB.getDefaultInstance()));
    PromoteAutoFlags task = AbstractTaskBase.createTask(PromoteAutoFlags.class);
    task.initialize(params);
    task.run();
  }
}
