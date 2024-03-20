// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.yb.WireProtocol;
import org.yb.client.GetAutoFlagsConfigResponse;
import org.yb.master.MasterClusterOuterClass;

public class AutoFlagUtilTest extends FakeDBApplication {

  private AutoFlagUtil autoFlagUtil;
  private Universe defaultUniverse;

  @Before
  public void setUp() {
    autoFlagUtil = new AutoFlagUtil(mockGFlagsValidation, mockService);
    defaultUniverse = ModelFactory.createUniverse(ModelFactory.testCustomer().getId());
  }

  @Test
  public void testGetPromotedAutoFlag() throws Exception {
    Set<String> autoFlagsSet =
        ImmutableSet.of(
            "TEST_auto_flags_new_install",
            "TEST_auto_flags_initialized",
            "FLAG_1",
            "FLAG_2",
            "FLAG_3",
            "FLAG_4");
    GetAutoFlagsConfigResponse resp = getAutoFlagConfigResponse(autoFlagsSet, autoFlagsSet);
    when(mockYBClient.autoFlagsConfig()).thenReturn(resp);
    when(mockService.getClient(any(), any())).thenReturn(mockYBClient);

    List<GFlagsValidation.AutoFlagDetails> autoFlagDetails = new ArrayList<>();
    for (int flagClass = 1; flagClass <= 4; flagClass++) {
      GFlagsValidation.AutoFlagDetails flag = new GFlagsValidation.AutoFlagDetails();
      flag.name = "FLAG_" + flagClass;
      flag.flagClass = flagClass;
      autoFlagDetails.add(flag);
    }
    for (ServerType serverType : ImmutableSet.of(ServerType.MASTER, ServerType.TSERVER)) {
      GFlagsValidation.AutoFlagsPerServer flagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      flagsPerServer.serverType = serverType == ServerType.MASTER ? "yb-master" : "yb-tserver";
      flagsPerServer.autoFlagDetails = autoFlagDetails;
      when(mockGFlagsValidation.extractAutoFlags(anyString(), (ServerType) any()))
          .thenReturn(flagsPerServer);
      Set<String> expectedResult =
          autoFlagDetails.stream().map(flag -> flag.name).collect(Collectors.toSet());
      int flagClass = 0;
      while (flagClass <= 4) {
        assertEquals(
            expectedResult,
            autoFlagUtil.getPromotedAutoFlags(defaultUniverse, serverType, flagClass));
        flagClass++;
        expectedResult.remove("FLAG_" + flagClass);
      }
    }
  }

  @Test
  public void testUpgradeRequireFinalize() throws Exception {

    GFlagsValidation.AutoFlagDetails flag1 = new GFlagsValidation.AutoFlagDetails();
    flag1.name = "FLAG_1";
    flag1.flagClass = 1;
    GFlagsValidation.AutoFlagDetails flag2 = new GFlagsValidation.AutoFlagDetails();
    flag2.name = "FLAG_2";
    flag2.flagClass = 2;

    GFlagsValidation.AutoFlagsPerServer newAutoFlags = new GFlagsValidation.AutoFlagsPerServer();
    newAutoFlags.autoFlagDetails = Arrays.asList(flag1, flag2);
    GFlagsValidation.AutoFlagsPerServer oldAutoFlags = new GFlagsValidation.AutoFlagsPerServer();
    oldAutoFlags.autoFlagDetails = Arrays.asList(flag1);

    when(mockGFlagsValidation.extractAutoFlags(anyString(), (ServerType) any()))
        .thenReturn(oldAutoFlags)
        .thenReturn(newAutoFlags);
    assertTrue(autoFlagUtil.upgradeRequireFinalize("old-version", "new-version"));

    when(mockGFlagsValidation.extractAutoFlags(anyString(), (ServerType) any()))
        .thenReturn(newAutoFlags)
        .thenReturn(newAutoFlags);
    assertFalse(autoFlagUtil.upgradeRequireFinalize("old-version", "new-version"));

    when(mockGFlagsValidation.getYsqlMigrationFilesList(anyString()))
        .thenReturn(ImmutableSet.of("ysql_1"))
        .thenReturn(ImmutableSet.of("ysql_1", "ysql_2"));
    assertTrue(autoFlagUtil.upgradeRequireFinalize("old-version", "new-version"));

    when(mockGFlagsValidation.getYsqlMigrationFilesList(anyString()))
        .thenReturn(ImmutableSet.of("ysql_1", "ysql_2"))
        .thenReturn(ImmutableSet.of("ysql_1", "ysql_2"));
    assertFalse(autoFlagUtil.upgradeRequireFinalize("old-version", "new-version"));
  }

  private GetAutoFlagsConfigResponse getAutoFlagConfigResponse(
      Set<String> masterAutoFlag, Set<String> tserverAutoFlags) {
    List<WireProtocol.PromotedFlagsPerProcessPB> flags = new ArrayList<>();
    WireProtocol.AutoFlagsConfigPB config =
        MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder()
            .getConfigBuilder()
            .addPromotedFlags(
                0,
                WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
                    .addAllFlags(masterAutoFlag)
                    .setProcessName("yb-master")
                    .build())
            .addPromotedFlags(
                1,
                WireProtocol.PromotedFlagsPerProcessPB.newBuilder()
                    .addAllFlags(tserverAutoFlags)
                    .setProcessName("yb-tserver")
                    .build())
            .setConfigVersion(1)
            .build();

    MasterClusterOuterClass.GetAutoFlagsConfigResponsePB responsePB =
        MasterClusterOuterClass.GetAutoFlagsConfigResponsePB.newBuilder().setConfig(config).build();
    return new GetAutoFlagsConfigResponse(0, null, responsePB);
  }
}
