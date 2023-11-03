// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.validators;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

@Singleton
public class OnPremValidator extends ProviderFieldsValidator {

  private final RuntimeConfGetter runtimeConfigGetter;
  private final String zoneNameRegex = "^(?![_\\-])[\\w\\s\\-]+(?<![_\\-\\s])$";

  @Inject
  public OnPremValidator(BeanValidator beanValidator, RuntimeConfGetter runtimeConfigGetter) {
    super(beanValidator, runtimeConfigGetter);
    this.runtimeConfigGetter = runtimeConfigGetter;
  }

  @Override
  public void validate(Provider provider) {
    if (provider.getRegions() != null && !provider.getRegions().isEmpty()) {
      for (Region region : provider.getRegions()) {
        if (region.getZones() != null && !region.getZones().isEmpty()) {
          // Validate the zone names here.
          for (AvailabilityZone zone : region.getZones()) {
            validate(zone, String.format("ZONE.%s", region.getZones().indexOf(zone)));
          }
        }
      }
    }
  }

  private void validate(AvailabilityZone zone, String key) {
    String name = zone.getName();
    String code = zone.getCode();
    if (!name.matches(zoneNameRegex)) {
      throwBeanProviderValidatorError(
          key, "Zone name cannot contain any special characters except '-' and '_'.");
    }

    if (!code.matches(zoneNameRegex)) {
      throwBeanProviderValidatorError(
          key, "Zone code cannot contain any special characters except '-' and '_'.");
    }
  }

  @Override
  public void validate(AvailabilityZone zone) {
    validate(zone, "ZONE");
  }
}
