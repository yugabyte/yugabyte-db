// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.utils;

import static org.junit.Assert.assertEquals;

import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class CapacityReservationUtilTest {

  @Parameters({
    "Standard_DS2_v2, true",
    "Standard_D8as_v4, true",
    "Standard_D10as_v5, true",
    "Standard_D2, false",
    "Standard_D8ads_v4, false", // too low version.
    "Standard_NV8as_v4, false"
  })
  @Test
  public void testSupportedInstances(String instanceType, boolean availability) {
    assertEquals(
        availability, CapacityReservationUtil.azureCheckInstanceTypeIsSupported(instanceType));
  }
}
