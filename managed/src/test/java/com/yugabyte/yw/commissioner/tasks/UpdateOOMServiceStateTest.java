// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UpdateOOMServiceState.EarlyoomEnablementState;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.ProviderConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.AdditionalServicesStateData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ProviderSpecification;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import java.util.Arrays;
import java.util.Collections;
import org.junit.Before;
import org.junit.Test;

public class UpdateOOMServiceStateTest extends FakeDBApplication {

  private static final String EARLYOOM_ARGS = "-M 1024 -m 50";
  private static final String OTHER_EARLYOOM_ARGS = "-M 2048 -m 25";

  private Customer customer;
  private Provider awsProvider;
  private Provider azuProvider;
  private RuntimeConfGetter confGetter;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    awsProvider = ModelFactory.awsProvider(customer);
    azuProvider = ModelFactory.azuProvider(customer);
    confGetter = mock(RuntimeConfGetter.class);
  }

  @Test
  public void featureFlagDisabledReturnsDefaults() {
    stubCustomerFeature(false);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, singleProviderParams(awsProvider), customer);

    assertDisabled(state);
  }

  @Test
  public void singleProviderAllFlagsEnabledUsesParsedConfig() {
    stubCustomerFeature(true);
    stubProvider(awsProvider, true, true, EARLYOOM_ARGS);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, singleProviderParams(awsProvider), customer);

    assertTrue(state.isInstallationPossible());
    assertTrue(state.isEnableByDefault());
    assertTrue(state.isEnableOnUpgrade());
    assertEquals(AdditionalServicesStateData.fromArgs(EARLYOOM_ARGS, true), state.getConfig());
  }

  @Test
  public void enableOnUpgradeRequiresEnableByDefault() {
    stubCustomerFeature(true);
    stubProvider(awsProvider, false, true, EARLYOOM_ARGS);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, singleProviderParams(awsProvider), customer);

    assertTrue(state.isInstallationPossible());
    assertFalse(state.isEnableByDefault());
    assertFalse(state.isEnableOnUpgrade());
    assertEquals(AdditionalServicesStateData.fromArgs(EARLYOOM_ARGS, true), state.getConfig());
  }

  @Test
  public void enableByDefaultWithoutUpgradeFlag() {
    stubCustomerFeature(true);
    stubProvider(awsProvider, true, false, EARLYOOM_ARGS);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, singleProviderParams(awsProvider), customer);

    assertTrue(state.isInstallationPossible());
    assertTrue(state.isEnableByDefault());
    assertFalse(state.isEnableOnUpgrade());
  }

  @Test
  public void kubernetesProviderMakesInstallationImpossible() {
    Provider k8sProvider = ModelFactory.newProvider(customer, CloudType.kubernetes);
    stubCustomerFeature(true);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, singleProviderParams(k8sProvider), customer);

    assertDisabled(state);
  }

  @Test
  public void manualOnpremMakesInstallationImpossible() {
    Provider onprem = ModelFactory.newProvider(customer, CloudType.onprem);
    onprem.getDetails().skipProvisioning = true;
    onprem.save();
    stubCustomerFeature(true);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, singleProviderParams(onprem), customer);

    assertDisabled(state);
  }

  @Test
  public void nonManualOnpremAllowsInstallation() {
    Provider onprem = ModelFactory.newProvider(customer, CloudType.onprem);
    onprem.getDetails().skipProvisioning = false;
    onprem.save();
    stubCustomerFeature(true);
    stubProvider(onprem, true, true, EARLYOOM_ARGS);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, singleProviderParams(onprem), customer);

    assertTrue(state.isInstallationPossible());
    assertTrue(state.isEnableByDefault());
    assertTrue(state.isEnableOnUpgrade());
  }

  @Test
  public void multiProviderAgreementEnablesAll() {
    stubCustomerFeature(true);
    stubProvider(awsProvider, true, true, EARLYOOM_ARGS);
    stubProvider(azuProvider, true, true, EARLYOOM_ARGS);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, multiProviderParams(awsProvider, azuProvider), customer);

    assertTrue(state.isInstallationPossible());
    assertTrue(state.isEnableByDefault());
    assertTrue(state.isEnableOnUpgrade());
    assertEquals(AdditionalServicesStateData.fromArgs(EARLYOOM_ARGS, true), state.getConfig());
  }

  @Test
  public void multiProviderDisagreesOnEnableByDefault() {
    stubCustomerFeature(true);
    stubProvider(awsProvider, true, true, EARLYOOM_ARGS);
    stubProvider(azuProvider, false, true, EARLYOOM_ARGS);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, multiProviderParams(awsProvider, azuProvider), customer);

    assertTrue(state.isInstallationPossible());
    assertFalse(state.isEnableByDefault());
    assertFalse(state.isEnableOnUpgrade());
    assertEquals(AdditionalServicesStateData.fromArgs(EARLYOOM_ARGS, true), state.getConfig());
  }

  @Test
  public void multiProviderDisagreesOnEnableOnUpgrade() {
    stubCustomerFeature(true);
    stubProvider(awsProvider, true, true, EARLYOOM_ARGS);
    stubProvider(azuProvider, true, false, EARLYOOM_ARGS);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, multiProviderParams(awsProvider, azuProvider), customer);

    assertTrue(state.isInstallationPossible());
    assertTrue(state.isEnableByDefault());
    assertFalse(state.isEnableOnUpgrade());
  }

  @Test
  public void multiProviderDivergentArgsLeavesEmptyConfig() {
    stubCustomerFeature(true);
    stubProvider(awsProvider, true, true, EARLYOOM_ARGS);
    stubProvider(azuProvider, true, true, OTHER_EARLYOOM_ARGS);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, multiProviderParams(awsProvider, azuProvider), customer);

    assertTrue(state.isInstallationPossible());
    assertTrue(state.isEnableByDefault());
    assertTrue(state.isEnableOnUpgrade());
    assertEquals(new AdditionalServicesStateData.EarlyoomConfig(), state.getConfig());
  }

  @Test
  public void multiProviderSingleProviderArgsUsesThatConfig() {
    stubCustomerFeature(true);
    stubProvider(awsProvider, true, true, EARLYOOM_ARGS);
    stubProvider(azuProvider, true, true, "");

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, multiProviderParams(awsProvider, azuProvider), customer);

    assertTrue(state.isInstallationPossible());
    assertEquals(AdditionalServicesStateData.fromArgs(EARLYOOM_ARGS, true), state.getConfig());
  }

  @Test
  public void kubernetesAmongProvidersMakesInstallationImpossible() {
    Provider k8sProvider = ModelFactory.newProvider(customer, CloudType.kubernetes);
    stubCustomerFeature(true);
    stubProvider(awsProvider, true, true, EARLYOOM_ARGS);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, multiProviderParams(awsProvider, k8sProvider), customer);

    assertDisabled(state);
  }

  @Test
  public void unsetProviderFlagsDoNotEnable() {
    stubCustomerFeature(true);
    when(confGetter.getConfForScope(
            eq(awsProvider), eq(ProviderConfKeys.enableEarlyoomByDefaultForProvider)))
        .thenReturn(null);
    when(confGetter.getConfForScope(
            eq(awsProvider), eq(ProviderConfKeys.enableEarlyoomOnOSUpgrade)))
        .thenReturn(null);
    when(confGetter.getConfForScope(eq(awsProvider), eq(ProviderConfKeys.earlyoomDefaultArgs)))
        .thenReturn(null);

    EarlyoomEnablementState state =
        UpdateOOMServiceState.getEarlyoomEnablementState(
            confGetter, singleProviderParams(awsProvider), customer);

    assertTrue(state.isInstallationPossible());
    assertFalse(state.isEnableByDefault());
    assertFalse(state.isEnableOnUpgrade());
    assertEquals(new AdditionalServicesStateData.EarlyoomConfig(), state.getConfig());
  }

  private void stubCustomerFeature(boolean enabled) {
    when(confGetter.getConfForScope(eq(customer), eq(CustomerConfKeys.enableEarlyoomFeature)))
        .thenReturn(enabled);
  }

  private void stubProvider(
      Provider provider, Boolean enableByDefault, Boolean enableOnUpgrade, String earlyoomArgs) {
    when(confGetter.getConfForScope(
            eq(provider), eq(ProviderConfKeys.enableEarlyoomByDefaultForProvider)))
        .thenReturn(enableByDefault);
    when(confGetter.getConfForScope(eq(provider), eq(ProviderConfKeys.enableEarlyoomOnOSUpgrade)))
        .thenReturn(enableOnUpgrade);
    when(confGetter.getConfForScope(eq(provider), eq(ProviderConfKeys.earlyoomDefaultArgs)))
        .thenReturn(earlyoomArgs);
  }

  private static UniverseDefinitionTaskParams singleProviderParams(Provider provider) {
    UserIntent userIntent = new UserIntent();
    userIntent.provider = provider.getUuid().toString();
    userIntent.providerType = provider.getCloudCode();
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.clusters = Collections.singletonList(new Cluster(ClusterType.PRIMARY, userIntent));
    return params;
  }

  private static UniverseDefinitionTaskParams multiProviderParams(
      Provider primary, Provider secondary) {
    UserIntent userIntent = new UserIntent();
    ProviderSpecification primarySpec = new ProviderSpecification();
    primarySpec.setProviderUUID(primary.getUuid());
    primarySpec.setProviderType(primary.getCloudCode());
    ProviderSpecification secondarySpec = new ProviderSpecification();
    secondarySpec.setProviderUUID(secondary.getUuid());
    secondarySpec.setProviderType(secondary.getCloudCode());
    userIntent.providerSpecifications = Arrays.asList(primarySpec, secondarySpec);

    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.clusters = Collections.singletonList(new Cluster(ClusterType.PRIMARY, userIntent));
    return params;
  }

  private static void assertDisabled(EarlyoomEnablementState state) {
    assertFalse(state.isInstallationPossible());
    assertFalse(state.isEnableByDefault());
    assertFalse(state.isEnableOnUpgrade());
    assertEquals(new AdditionalServicesStateData.EarlyoomConfig(), state.getConfig());
  }
}
