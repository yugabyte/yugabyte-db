// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static junit.framework.TestCase.fail;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.services.config.YbClientConfigFactory;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class XClusterUniverseServiceTest extends FakeDBApplication {

  private Universe defaultUniverse;
  private XClusterUniverseService xClusterUniverseService;
  @Mock private PlatformExecutorFactory mockPlatformExecutorFactory;
  @Mock private YbClientConfigFactory ybClientConfigFactory;
  @Mock private YBClientService ybService;
  @Mock private AutoFlagUtil mockAutoFlagUtil;
  @Mock RuntimeConfGetter mockConfGetter;

  @Before
  public void setup() {
    ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse("univ-1");
    TestHelper.updateUniverseVersion(defaultUniverse, "2.17.0.0-b1");
    xClusterUniverseService =
        new XClusterUniverseService(
            mockGFlagsValidation,
            mockConfGetter,
            ybService,
            mockAutoFlagUtil,
            mockPlatformExecutorFactory,
            ybClientConfigFactory);
  }

  @Test
  public void testGetXClusterConnectedUniverses() {
    Set<Universe> expectedUniverseSet = new HashSet<>();
    expectedUniverseSet.add(defaultUniverse);
    assertEquals(
        expectedUniverseSet,
        xClusterUniverseService.getXClusterConnectedUniverses(defaultUniverse));
    Universe universe2 = ModelFactory.createUniverse("univ-2");
    XClusterConfig.create("test", defaultUniverse.getUniverseUUID(), universe2.getUniverseUUID());
    Universe universe3 = ModelFactory.createUniverse("univ3");
    XClusterConfig.create("test-2", universe3.getUniverseUUID(), universe2.getUniverseUUID());
    Universe universe4 = ModelFactory.createUniverse("univ-4");
    XClusterConfig.create("test-3", universe4.getUniverseUUID(), defaultUniverse.getUniverseUUID());
    expectedUniverseSet.addAll(Arrays.asList(universe2, universe3, universe4));
    assertEquals(
        expectedUniverseSet,
        xClusterUniverseService.getXClusterConnectedUniverses(defaultUniverse));
    assertEquals(
        expectedUniverseSet, xClusterUniverseService.getXClusterConnectedUniverses(universe4));
    XClusterConfig.create("test-5", universe2.getUniverseUUID(), universe4.getUniverseUUID());
    assertEquals(
        expectedUniverseSet,
        xClusterUniverseService.getXClusterConnectedUniverses(defaultUniverse));
  }

  @Test
  public void testCanPromoteAutoFlags() {
    Universe universe2 = ModelFactory.createUniverse("univ-2");
    TestHelper.updateUniverseVersion(defaultUniverse, "2.17.0.0-b1");
    TestHelper.updateUniverseVersion(universe2, "2.17.0.0-b1");
    Set<Universe> universeSet = Stream.of(defaultUniverse, universe2).collect(Collectors.toSet());
    try {
      when(mockConfGetter.getConfForScope(
              any(Universe.class), eq(UniverseConfKeys.promoteAutoFlag)))
          .thenReturn(true);
      when(mockConfGetter.getConfForScope(
              any(Universe.class), eq(UniverseConfKeys.enableRollbackSupport)))
          .thenReturn(false);
      GFlagsValidation.AutoFlagDetails flag = new GFlagsValidation.AutoFlagDetails();
      flag.name = "FLAG_1";
      GFlagsValidation.AutoFlagsPerServer flagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      flagsPerServer.autoFlagDetails = Collections.singletonList(flag);
      when(mockGFlagsValidation.extractAutoFlags(anyString(), anyString()))
          .thenReturn(flagsPerServer);
      assertTrue(
          xClusterUniverseService.canPromoteAutoFlags(universeSet, defaultUniverse, "2.17.0.0-b1"));
      GFlagsValidation.AutoFlagsPerServer flagsPerServer2 =
          new GFlagsValidation.AutoFlagsPerServer();
      GFlagsValidation.AutoFlagDetails flag2 = new GFlagsValidation.AutoFlagDetails();
      flag2.name = "FLAG_2";
      flagsPerServer2.autoFlagDetails = Collections.singletonList(flag2);
      when(mockGFlagsValidation.extractAutoFlags(anyString(), anyString()))
          .thenReturn(flagsPerServer)
          .thenReturn(flagsPerServer)
          .thenReturn(flagsPerServer2)
          .thenReturn(flagsPerServer2);
      assertFalse(
          xClusterUniverseService.canPromoteAutoFlags(universeSet, defaultUniverse, "2.17.0.0-b1"));
    } catch (Exception e) {
      fail();
    }
  }

  @Test
  public void testGetActiveXClusterSourceAndTargetUniverseSet() {
    Universe universe2 = ModelFactory.createUniverse("univ-2");
    XClusterConfig.create("test", defaultUniverse.getUniverseUUID(), universe2.getUniverseUUID());
    Universe universe3 = ModelFactory.createUniverse("univ3");
    XClusterConfig.create("test-2", universe3.getUniverseUUID(), defaultUniverse.getUniverseUUID());
    Universe universe4 = ModelFactory.createUniverse("univ-4");
    XClusterConfig.create("test-3", universe4.getUniverseUUID(), defaultUniverse.getUniverseUUID())
        .updateStatus(XClusterConfig.XClusterConfigStatusType.DeletedUniverse);
    Universe universe5 = ModelFactory.createUniverse("univ-5");
    XClusterConfig excludeConfig =
        XClusterConfig.create(
            "test-4", universe5.getUniverseUUID(), defaultUniverse.getUniverseUUID());
    assertEquals(
        new HashSet<>(
            Stream.of(universe2, universe3)
                .map(Universe::getUniverseUUID)
                .collect(Collectors.toSet())),
        xClusterUniverseService.getActiveXClusterSourceAndTargetUniverseSet(
            defaultUniverse.getUniverseUUID(), Collections.singleton(excludeConfig.getUuid())));
  }

  @Test
  public void testGetMultipleXClusterConnectedUniverseSet() {
    Universe universe2 = ModelFactory.createUniverse("univ-2");
    XClusterConfig.create("test", defaultUniverse.getUniverseUUID(), universe2.getUniverseUUID());
    Universe universe3 = ModelFactory.createUniverse("univ3");
    XClusterConfig.create("test-2", universe3.getUniverseUUID(), universe2.getUniverseUUID());
    Universe universe4 = ModelFactory.createUniverse("univ-4");
    XClusterConfig.create("test-3", universe4.getUniverseUUID(), universe2.getUniverseUUID());
    Universe universe5 = ModelFactory.createUniverse("univ-5");
    Universe universe6 = ModelFactory.createUniverse("univ-6");
    XClusterConfig.create("test-4", universe6.getUniverseUUID(), universe5.getUniverseUUID());
    Universe universe7 = ModelFactory.createUniverse("univ-7");
    Universe universe8 = ModelFactory.createUniverse("univ-8");
    XClusterConfig excludeConfig =
        XClusterConfig.create("test-5", universe7.getUniverseUUID(), universe8.getUniverseUUID());
    Set<Set<Universe>> expectedSet = new HashSet<>();
    expectedSet.add(
        Stream.of(defaultUniverse, universe2, universe3, universe4).collect(Collectors.toSet()));
    expectedSet.add(Stream.of(universe5, universe6).collect(Collectors.toSet()));
    expectedSet.add(Stream.of(universe7).collect(Collectors.toSet()));
    assertEquals(
        expectedSet,
        xClusterUniverseService.getMultipleXClusterConnectedUniverseSet(
            Stream.of(
                    defaultUniverse,
                    universe2,
                    universe3,
                    universe4,
                    universe5,
                    universe6,
                    universe7)
                .map(Universe::getUniverseUUID)
                .collect(Collectors.toSet()),
            Collections.singleton(excludeConfig.getUuid())));
  }

  @Test
  public void testGetImpactedTargetUniverseWithUpgradeFinalize() throws Exception {
    Universe univ1 = ModelFactory.createUniverse("univ-2");
    XClusterConfig.create("test-1", defaultUniverse.getUniverseUUID(), univ1.getUniverseUUID());
    TestHelper.updateUniverseVersion(univ1, "2.16.0.0-b1");
    Universe univ2 = ModelFactory.createUniverse("univ-3");
    XClusterConfig.create("test-2", defaultUniverse.getUniverseUUID(), univ2.getUniverseUUID());
    TestHelper.updateUniverseVersion(univ2, "2.24.0.0-b1");

    GFlagsValidation.AutoFlagDetails flag1 = new GFlagsValidation.AutoFlagDetails();
    flag1.name = "FLAG_1";
    flag1.flagClass = 3;
    GFlagsValidation.AutoFlagDetails flag2 = new GFlagsValidation.AutoFlagDetails();
    flag2.name = "FLAG_2";
    flag2.flagClass = 4;
    GFlagsValidation.AutoFlagsPerServer flagsPerServer = new GFlagsValidation.AutoFlagsPerServer();
    flagsPerServer.autoFlagDetails = Arrays.asList(flag1, flag2);
    when(mockGFlagsValidation.extractAutoFlags(anyString(), (UniverseTaskBase.ServerType) any()))
        .thenReturn(flagsPerServer);

    when(mockAutoFlagUtil.getPromotedAutoFlags(any(), any(), anyInt()))
        .thenReturn(ImmutableSet.of("FLAG_1", "FLAG_2"));
    assertEquals(
        ImmutableSet.of(univ1.getUniverseUUID()),
        xClusterUniverseService.getXClusterTargetUniverseSetToBeImpactedWithUpgradeFinalize(
            defaultUniverse));

    when(mockAutoFlagUtil.getPromotedAutoFlags(any(), any(), anyInt()))
        .thenReturn(ImmutableSet.of("FLAG_1"));
    assertEquals(
        ImmutableSet.of(univ1.getUniverseUUID(), univ2.getUniverseUUID()),
        xClusterUniverseService.getXClusterTargetUniverseSetToBeImpactedWithUpgradeFinalize(
            defaultUniverse));
  }
}
