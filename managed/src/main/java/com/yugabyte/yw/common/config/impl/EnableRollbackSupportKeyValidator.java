// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.config.impl;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableSet;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigPreChangeValidator;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.Set;
import java.util.UUID;

@Singleton
public class EnableRollbackSupportKeyValidator implements RuntimeConfigPreChangeValidator {
  @Override
  public String getKeyPath() {
    return UniverseConfKeys.enableRollbackSupport.getKey();
  }

  private static Set<SoftwareUpgradeState> ROLLBACK_SUPPORT_STATES =
      ImmutableSet.of(
          SoftwareUpgradeState.PreFinalize,
          SoftwareUpgradeState.RollingBack,
          SoftwareUpgradeState.RollbackFailed,
          SoftwareUpgradeState.Finalizing,
          SoftwareUpgradeState.FinalizeFailed);

  @Override
  public void validateConfigGlobal(UUID scopeUUID, String path, String newValue) {
    Universe.getAllUUIDs()
        .forEach(
            uuid -> {
              validateUniverseState(Universe.getOrBadRequest(uuid), newValue);
            });
  }

  @Override
  public void validateConfigCustomer(
      Customer customer, UUID scopeUUID, String path, String newValue) {
    Universe.getAllUUIDs(customer)
        .forEach(
            uuid -> {
              validateUniverseState(Universe.getOrBadRequest(uuid), newValue);
            });
  }

  @Override
  public void validateConfigUniverse(
      Universe universe, UUID scopeUUID, String path, String newValue) {
    validateUniverseState(universe, newValue);
  }

  @Override
  public void validateDeleteConfig(UUID scopeUUID, String path) {
    throw new PlatformServiceException(
        BAD_REQUEST, "Cannot delete 'yb.upgrade.enable_rollback_support'.");
  }

  private void validateUniverseState(Universe universe, String newValue) {
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (universeDetails.softwareUpgradeState != null
        && ROLLBACK_SUPPORT_STATES.contains(universeDetails.softwareUpgradeState)
        && newValue.equals("false")) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "'yb.upgrade.enable_rollback_support' cannot be set to false as rollback support is"
              + " enabled on universe "
              + universe.getName());
    }
  }
}
