// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class ProxyConfigUpdateParamsTest {

  @Test
  public void testKubernetesUpgradeIsSupported() {
    assertTrue(new ProxyConfigUpdateParams().isKubernetesUpgradeSupported());
  }
}
