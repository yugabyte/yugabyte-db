// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.gflags.AutoFlagUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class SoftwareUpgradeHelperTest {

  private static final String PG11_VERSION = "2024.2.3.0-b116";
  private static final String PG15_VERSION = "2025.1.1.0-b73";

  @Mock private GFlagsValidation gFlagsValidation;

  private SoftwareUpgradeHelper helper;

  @Before
  public void setUp() {
    when(gFlagsValidation.ysqlMajorVersionUpgrade(anyString(), anyString())).thenReturn(true);
    helper =
        new SoftwareUpgradeHelper(
            mock(YBClientService.class), gFlagsValidation, mock(AutoFlagUtil.class));
  }

  @Test
  public void testSuperUserNotRequiredWhenYsqlDisabledDedicatedTls() {
    Universe universe =
        universeWith(
            false /* enableYSQL */, true /* tls */, true /* dedicatedNodes */, CloudType.local);
    assertFalse(helper.isSuperUserRequiredForCatalogUpgrade(universe, PG11_VERSION, PG15_VERSION));
  }

  @Test
  public void testSuperUserNotRequiredWhenYsqlDisabledKubernetesTls() {
    Universe universe =
        universeWith(
            false /* enableYSQL */,
            true /* tls */,
            false /* dedicatedNodes */,
            CloudType.kubernetes);
    assertFalse(helper.isSuperUserRequiredForCatalogUpgrade(universe, PG11_VERSION, PG15_VERSION));
  }

  @Test
  public void testSuperUserRequiredWhenYsqlEnabledDedicatedTls() {
    Universe universe =
        universeWith(
            true /* enableYSQL */, true /* tls */, true /* dedicatedNodes */, CloudType.local);
    assertTrue(helper.isSuperUserRequiredForCatalogUpgrade(universe, PG11_VERSION, PG15_VERSION));
  }

  @Test
  public void testSuperUserRequiredWhenYsqlEnabledKubernetesTls() {
    Universe universe =
        universeWith(
            true /* enableYSQL */,
            true /* tls */,
            false /* dedicatedNodes */,
            CloudType.kubernetes);
    assertTrue(helper.isSuperUserRequiredForCatalogUpgrade(universe, PG11_VERSION, PG15_VERSION));
  }

  @Test
  public void testYsqlMajorUpgradeNotRequiredWhenYsqlDisabled() {
    Universe universe =
        universeWith(
            false /* enableYSQL */, true /* tls */, true /* dedicatedNodes */, CloudType.local);
    assertFalse(helper.isYsqlMajorVersionUpgradeRequired(universe, PG11_VERSION, PG15_VERSION));
  }

  private static Universe universeWith(
      boolean enableYSQL, boolean tls, boolean dedicatedNodes, CloudType providerType) {
    Universe universe = mock(Universe.class);
    UniverseDefinitionTaskParams details = new UniverseDefinitionTaskParams();
    UserIntent intent = new UserIntent();
    intent.enableYSQL = enableYSQL;
    intent.enableYCQL = true;
    intent.enableNodeToNodeEncrypt = tls;
    intent.enableClientToNodeEncrypt = tls;
    intent.dedicatedNodes = dedicatedNodes;
    intent.providerType = providerType;
    details.upsertPrimaryCluster(intent, null, null);
    when(universe.getUniverseDetails()).thenReturn(details);
    return universe;
  }
}
