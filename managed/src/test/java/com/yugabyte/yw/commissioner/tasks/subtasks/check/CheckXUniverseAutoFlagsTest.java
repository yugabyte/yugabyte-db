// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doCallRealMethod;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import java.util.Arrays;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.yb.WireProtocol;
import org.yb.client.GetAutoFlagsConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterClusterOuterClass;

public class CheckXUniverseAutoFlagsTest extends CommissionerBaseTest {

  private Universe sourceUniverse;
  private Universe targetUniverse;
  private YBClient mockClient;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    sourceUniverse = ModelFactory.createUniverse("source-universe");
    targetUniverse = ModelFactory.createUniverse("target-universe");
    mockClient = mock(YBClient.class);
    try {
      when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
      doCallRealMethod()
          .when(mockGFlagsValidation)
          .getFilteredAutoFlagsWithNonInitialValue(anyMap(), anyString(), any());
      doCallRealMethod().when(mockGFlagsValidation).isAutoFlag(any());
    } catch (Exception ignored) {
      fail();
    }
  }

  @Test
  public void testAutoFlagCheckSuccess() throws Exception {
    WireProtocol.PromotedFlagsPerProcessPB masterFlagPB =
        WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
            .addFlags("FLAG_1")
            .setProcessName("yb-master")
            .build();
    WireProtocol.PromotedFlagsPerProcessPB tserverFlagPB =
        WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
            .addFlags("FLAG_1")
            .setProcessName("yb-tserver")
            .build();
    WireProtocol.AutoFlagsConfigPB config =
        MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder()
            .getConfigBuilder()
            .addPromotedFlags(masterFlagPB)
            .addPromotedFlags(tserverFlagPB)
            .setConfigVersion(1)
            .build();
    MasterClusterOuterClass.GetAutoFlagsConfigResponsePB responsePB =
        MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder().setConfig(config).build();
    GetAutoFlagsConfigResponse resp = new GetAutoFlagsConfigResponse(0, null, responsePB);
    lenient().when(mockClient.autoFlagsConfig()).thenReturn(resp);
    updateGFlags(sourceUniverse, ImmutableMap.of("FLAG_2", "value"), null);
    updateGFlags(sourceUniverse, ImmutableMap.of("FLAG_2", "value"), null);
    GFlagDetails flagDetails = new GFlagDetails();
    flagDetails.name = "FLAG_1";
    flagDetails.target = "value";
    flagDetails.initial = "initial";
    flagDetails.tags = "auto";
    GFlagDetails flagDetails2 = new GFlagDetails();
    flagDetails2.name = "FLAG_2";
    flagDetails2.target = "value";
    flagDetails2.initial = "initial";
    flagDetails2.tags = "auto";
    when(mockGFlagsValidation.listAllAutoFlags(anyString(), anyString()))
        .thenReturn(Arrays.asList(flagDetails, flagDetails2));
    when(mockGFlagsValidation.extractGFlags(anyString(), anyString(), anyBoolean()))
        .thenReturn(Arrays.asList(flagDetails, flagDetails2));
    CheckXUniverseAutoFlags task = AbstractTaskBase.createTask(CheckXUniverseAutoFlags.class);
    CheckXUniverseAutoFlags.Params params = new CheckXUniverseAutoFlags.Params();
    params.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    params.targetUniverseUUID = targetUniverse.getUniverseUUID();
    task.initialize(params);
    task.run();
  }

  @Test
  public void testAutoFlagCheckSuccessWithOverriddenAutoFlag() throws Exception {
    WireProtocol.PromotedFlagsPerProcessPB masterFlagPB =
        WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
            .addFlags("FLAG_1")
            .setProcessName("yb-master")
            .build();
    WireProtocol.PromotedFlagsPerProcessPB tserverFlagPB =
        WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
            .addFlags("FLAG_1")
            .setProcessName("yb-tserver")
            .build();
    WireProtocol.AutoFlagsConfigPB config =
        MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder()
            .getConfigBuilder()
            .addPromotedFlags(masterFlagPB)
            .addPromotedFlags(tserverFlagPB)
            .setConfigVersion(1)
            .build();
    MasterClusterOuterClass.GetAutoFlagsConfigResponsePB responsePB =
        MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder().setConfig(config).build();
    GetAutoFlagsConfigResponse resp = new GetAutoFlagsConfigResponse(0, null, responsePB);
    lenient().when(mockClient.autoFlagsConfig()).thenReturn(resp);
    updateGFlags(sourceUniverse, ImmutableMap.of("FLAG_2", "intial"), null);
    updateGFlags(sourceUniverse, ImmutableMap.of("FLAG_2", "initial"), null);
    doCallRealMethod()
        .when(mockGFlagsValidation)
        .getFilteredAutoFlagsWithNonInitialValue(anyMap(), anyString(), any());
    GFlagDetails flagDetails = new GFlagDetails();
    flagDetails.name = "FLAG_1";
    flagDetails.target = "target";
    flagDetails.initial = "initial";
    flagDetails.tags = "auto";
    GFlagDetails flagDetails2 = new GFlagDetails();
    flagDetails2.name = "FLAG_2";
    flagDetails2.target = "target";
    flagDetails2.initial = "initial";
    flagDetails2.tags = "auto";
    when(mockGFlagsValidation.extractGFlags(anyString(), anyString(), anyBoolean()))
        .thenReturn(Arrays.asList(flagDetails, flagDetails2));
    when(mockGFlagsValidation.listAllAutoFlags(anyString(), anyString()))
        .thenReturn(Arrays.asList(flagDetails));
    CheckXUniverseAutoFlags task = AbstractTaskBase.createTask(CheckXUniverseAutoFlags.class);
    CheckXUniverseAutoFlags.Params params = new CheckXUniverseAutoFlags.Params();
    params.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    params.targetUniverseUUID = targetUniverse.getUniverseUUID();
    task.initialize(params);
    task.run();
  }

  @Test
  public void testAutoFlagCheckFailureWithOverriddenAutoFlag() throws Exception {
    WireProtocol.PromotedFlagsPerProcessPB masterFlagPB =
        WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
            .addFlags("FLAG_1")
            .setProcessName("yb-master")
            .build();
    WireProtocol.PromotedFlagsPerProcessPB tserverFlagPB =
        WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
            .addFlags("FLAG_1")
            .setProcessName("yb-tserver")
            .build();
    WireProtocol.AutoFlagsConfigPB config =
        MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder()
            .getConfigBuilder()
            .addPromotedFlags(masterFlagPB)
            .addPromotedFlags(tserverFlagPB)
            .setConfigVersion(1)
            .build();
    MasterClusterOuterClass.GetAutoFlagsConfigResponsePB responsePB =
        MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder().setConfig(config).build();
    GetAutoFlagsConfigResponse resp = new GetAutoFlagsConfigResponse(0, null, responsePB);
    lenient().when(mockClient.autoFlagsConfig()).thenReturn(resp);
    updateGFlags(sourceUniverse, ImmutableMap.of("FLAG_2", "target"), null);
    updateGFlags(sourceUniverse, ImmutableMap.of("FLAG_2", "target"), null);
    GFlagDetails flagDetails = new GFlagDetails();
    flagDetails.name = "FLAG_1";
    flagDetails.target = "target";
    flagDetails.initial = "initial";
    flagDetails.tags = "auto";
    GFlagDetails flagDetails2 = new GFlagDetails();
    flagDetails2.name = "FLAG_2";
    flagDetails2.target = "target";
    flagDetails2.initial = "initial";
    flagDetails2.tags = "auto";
    when(mockGFlagsValidation.extractGFlags(anyString(), anyString(), anyBoolean()))
        .thenReturn(Arrays.asList(flagDetails, flagDetails2));
    when(mockGFlagsValidation.listAllAutoFlags(anyString(), anyString()))
        .thenReturn(Arrays.asList(flagDetails, flagDetails2))
        .thenReturn(Arrays.asList(flagDetails));
    CheckXUniverseAutoFlags task = AbstractTaskBase.createTask(CheckXUniverseAutoFlags.class);
    CheckXUniverseAutoFlags.Params params = new CheckXUniverseAutoFlags.Params();
    params.sourceUniverseUUID = sourceUniverse.getUniverseUUID();
    params.targetUniverseUUID = targetUniverse.getUniverseUUID();
    task.initialize(params);
    PlatformServiceException pe = assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(BAD_REQUEST, pe.getHttpStatus());
    assertEquals(
        "Auto Flag FLAG_2 set on universe "
            + sourceUniverse.getUniverseUUID()
            + " is not present on universe "
            + targetUniverse.getUniverseUUID(),
        pe.getMessage());
  }

  private void updateGFlags(
      Universe universe, Map<String, String> gflags, UniverseTaskBase.ServerType serverType) {
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    if (serverType == null) {
      universe.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags = gflags;
      universe.getUniverseDetails().getPrimaryCluster().userIntent.tserverGFlags = gflags;
    } else if (serverType.equals(UniverseTaskBase.ServerType.MASTER)) {
      universe.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags = gflags;
    } else if (serverType.equals(UniverseTaskBase.ServerType.TSERVER)) {
      universe.getUniverseDetails().getPrimaryCluster().userIntent.tserverGFlags = gflags;
    }
    details.upsertPrimaryCluster(userIntent, null);
    universe.setUniverseDetails(details);
    universe.save();
  }
}
