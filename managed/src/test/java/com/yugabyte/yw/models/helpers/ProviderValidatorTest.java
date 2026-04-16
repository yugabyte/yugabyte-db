// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.helpers.provider.ProviderValidator;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class ProviderValidatorTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Customer customer;
  private Provider provider;
  private ProviderValidator providerValidator;
  private SettableRuntimeConfigFactory configFactory;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.onpremProvider(customer);
    providerValidator = app.injector().instanceOf(ProviderValidator.class);
    configFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
  }

  // Duplicate AZ code is added as a replacement of old deleted az (inactive).
  @Test
  public void testDuplicateOldAzReplacement() {
    Region region = Region.create(provider, "us-west-1", "us-west-1", "yb-image");
    AvailabilityZone az = AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    az.setActive(false);
    az.save();
    Provider requestedProvider = Provider.getOrBadRequest(provider.getUuid());
    List<Region> shallowCopy = new ArrayList<>(requestedProvider.getRegions());
    requestedProvider.setRegions(shallowCopy);
    Region newRegion = new Region();
    newRegion.setProvider(provider);
    newRegion.setCode("us-west-2");
    newRegion.setName("us-west-2");
    newRegion.setYbImage("yb-image");
    shallowCopy.add(newRegion);
    // New AZ code is same as old AZ code but old AZ is inactive. This should work as inactive AZs
    // are not
    AvailabilityZone newAz = new AvailabilityZone();
    newAz.setRegion(newRegion);
    newAz.setCode("az-1");
    newAz.setName("az-1");
    newAz.setSubnet("subnet-1");
    newRegion.setZones(Arrays.asList(newAz));
    providerValidator.validate(requestedProvider, provider);
  }

  // Duplicate AZ code is added. This must fail as new duplicates are not allowed.
  @Test
  @Parameters({
    "false", "true",
  })
  public void testDuplicateAzAddition(String allowExistingDuplicateAz) {
    Region region = Region.create(provider, "us-west-1", "us-west-1", "yb-image");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    Provider requestedProvider = Provider.getOrBadRequest(provider.getUuid());
    List<Region> shallowCopy = new ArrayList<>(requestedProvider.getRegions());
    requestedProvider.setRegions(shallowCopy);
    Region newRegion = new Region();
    newRegion.setProvider(provider);
    newRegion.setCode("us-west-2");
    newRegion.setName("us-west-2");
    newRegion.setYbImage("yb-image");
    shallowCopy.add(newRegion);
    AvailabilityZone newAz = new AvailabilityZone();
    // New AZ code is same as existing AZ code.
    newAz.setRegion(newRegion);
    newAz.setCode("az-1");
    newAz.setName("az-1");
    newAz.setSubnet("subnet-1");
    newRegion.setZones(Arrays.asList(newAz));
    configFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.allowExistingDuplicateAz.getKey(), allowExistingDuplicateAz);
    // This always fails as new duplicates are not allowed regardless of the config value.
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> providerValidator.validate(requestedProvider, provider));
    if (allowExistingDuplicateAz.equals("true")) {
      assertEquals(
          "Duplicate AZ code az-1. AZ code must be unique for a provider. Make sure to set 'uuid'"
              + " field for the existing AZs",
          exception.getMessage());
    } else {
      assertEquals(
          "Duplicate AZ code az-1. AZ code must be unique for a provider", exception.getMessage());
    }
  }

  // Existing AZ is updated to have duplicate code. This must fail as more duplicates are not
  // allowed.
  @Test
  @Parameters({
    "false", "true",
  })
  public void testDuplicateAzModification(String allowExistingDuplicateAz) {
    Region region = Region.create(provider, "us-west-1", "us-west-1", "yb-image");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    AvailabilityZone.createOrThrow(region, "az-2", "az-2", "subnet-1");
    Provider requestedProvider = Provider.getOrBadRequest(provider.getUuid());
    requestedProvider.getRegions().get(0).getZones().stream()
        .filter(az -> az.getCode().equals("az-1"))
        .forEach(az -> az.setCode("az-2"));
    configFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.allowExistingDuplicateAz.getKey(), allowExistingDuplicateAz);
    // This always fails as new duplicates are not allowed regardless of the config value.
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> providerValidator.validate(requestedProvider, provider));
    if (allowExistingDuplicateAz.equals("true")) {
      assertEquals(
          "Duplicate AZ code az-2. AZ code must be unique for a provider. Make sure to set 'uuid'"
              + " field for the existing AZs",
          exception.getMessage());
    } else {
      assertEquals(
          "Duplicate AZ code az-2. AZ code must be unique for a provider", exception.getMessage());
    }
  }

  // Add a new different AZ to an existing provider with already duplicated AZs. This must work to
  // support backward compatibility.
  @Test
  @Parameters({
    "false", "true",
  })
  public void testExistingDuplicateAz(String allowExistingDuplicateAz) {
    Region region = Region.create(provider, "us-west-1", "us-west-1", "yb-image");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    Provider requestedProvider = Provider.getOrBadRequest(provider.getUuid());
    List<Region> shallowCopy = new ArrayList<>(requestedProvider.getRegions());
    requestedProvider.setRegions(shallowCopy);
    Region newRegion = new Region();
    newRegion.setProvider(provider);
    newRegion.setCode("us-west-2");
    newRegion.setName("us-west-2");
    newRegion.setYbImage("yb-image");
    shallowCopy.add(newRegion);
    AvailabilityZone newAz = new AvailabilityZone();
    newAz.setRegion(newRegion);
    newAz.setCode("az-2");
    newAz.setName("az-2");
    newAz.setSubnet("subnet-1");
    newRegion.setZones(Arrays.asList(newAz));
    configFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.allowExistingDuplicateAz.getKey(), allowExistingDuplicateAz);
    if (allowExistingDuplicateAz.equals("true")) {
      // Old duplicates are allowed, but new AZ code is different so it should work.
      providerValidator.validate(requestedProvider, provider);
    } else {
      // This must fail as no duplicate is allowed irrespective of whether it is new or old.
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class,
              () -> providerValidator.validate(requestedProvider, provider));
      assertEquals(
          "Duplicate AZ code az-1. AZ code must be unique for a provider", exception.getMessage());
    }
  }

  // Add a duplicate AZ to an existing provider with already duplicated AZs. This must fail as new
  // duplicates are not allowed.
  @Test
  @Parameters({
    "false", "true",
  })
  public void testExistingDuplicateDuplicateAzAddition(String allowExistingDuplicateAz) {
    Region region = Region.create(provider, "us-west-1", "us-west-1", "yb-image");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    Provider requestedProvider = Provider.getOrBadRequest(provider.getUuid());
    List<Region> shallowCopy = new ArrayList<>(requestedProvider.getRegions());
    requestedProvider.setRegions(shallowCopy);
    Region newRegion = new Region();
    newRegion.setProvider(provider);
    newRegion.setCode("us-west-2");
    newRegion.setName("us-west-2");
    newRegion.setYbImage("yb-image");
    shallowCopy.add(newRegion);
    AvailabilityZone newAz = new AvailabilityZone();
    newAz.setRegion(newRegion);
    newAz.setCode("az-1");
    newAz.setName("az-1");
    newAz.setSubnet("subnet-1");
    newRegion.setZones(Arrays.asList(newAz));
    configFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.allowExistingDuplicateAz.getKey(), allowExistingDuplicateAz);
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> providerValidator.validate(requestedProvider, provider));
    if (allowExistingDuplicateAz.equals("true")) {
      assertEquals(
          "Duplicate AZ code az-1. AZ code must be unique for a provider. Make sure to set 'uuid'"
              + " field for the existing AZs",
          exception.getMessage());
    } else {
      assertEquals(
          "Duplicate AZ code az-1. AZ code must be unique for a provider", exception.getMessage());
    }
  }

  // Existing AZ is updated to have duplicate code for a provider with already duplicated AZs. This
  // must fail as more duplicates are not allowed.
  @Test
  @Parameters({
    "false", "true",
  })
  public void testDuplicateDuplicateAzModification(String allowExistingDuplicateAz) {
    Region region = Region.create(provider, "us-west-1", "us-west-1", "yb-image");
    AvailabilityZone.createOrThrow(region, "az-1", "az-1", "subnet-1");
    AvailabilityZone.createOrThrow(region, "az-2", "az-2", "subnet-1");
    AvailabilityZone.createOrThrow(region, "az-2", "az-2", "subnet-1");
    Provider requestedProvider = Provider.getOrBadRequest(provider.getUuid());
    requestedProvider.getRegions().get(0).getZones().stream()
        .filter(az -> az.getCode().equals("az-2"))
        .forEach(az -> az.setCode("az-1"));
    configFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.allowExistingDuplicateAz.getKey(), allowExistingDuplicateAz);
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> providerValidator.validate(requestedProvider, provider));
    if (allowExistingDuplicateAz.equals("true")) {
      assertEquals(
          "Duplicate AZ code az-1. AZ code must be unique for a provider. Make sure to set 'uuid'"
              + " field for the existing AZs",
          exception.getMessage());
    } else {
      assertEquals(
          "Duplicate AZ code az-1. AZ code must be unique for a provider", exception.getMessage());
    }
  }
}
