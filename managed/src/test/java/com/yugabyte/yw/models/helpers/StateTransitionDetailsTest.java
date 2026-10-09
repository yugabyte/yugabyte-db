// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import org.junit.Test;

public class StateTransitionDetailsTest {

  @Test
  public void applyBeforeToPreservesCurrentYbcFields() {
    UniverseDefinitionTaskParams before = new UniverseDefinitionTaskParams();
    before.setYbcSoftwareVersion("1.0.0-b1");
    before.setEnableYbc(true);
    before.setYbcInstalled(true);
    before.nodePrefix = "before-prefix";
    before.sequenceNumber = 2;

    UniverseDefinitionTaskParams target = new UniverseDefinitionTaskParams();
    target.setYbcSoftwareVersion("1.0.0-b1");
    target.setEnableYbc(true);
    target.setYbcInstalled(true);
    target.nodePrefix = "after-prefix";
    target.sequenceNumber = 2;

    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(before, target);
    StateTransitionDetails details = new StateTransitionDetails(true /* rollbackSafe */, delta);

    UniverseDefinitionTaskParams current = new UniverseDefinitionTaskParams();
    current.setYbcSoftwareVersion("2.0.0-b10");
    current.setEnableYbc(true);
    current.setYbcInstalled(true);
    current.updateInProgress = true;
    current.nodePrefix = "after-prefix";
    current.sequenceNumber = 4;

    UniverseDefinitionTaskParams restored = details.applyBeforeTo(current);

    assertEquals("2.0.0-b10", restored.getYbcSoftwareVersion());
    assertTrue(restored.isEnableYbc());
    assertTrue(restored.isYbcInstalled());
    assertEquals("before-prefix", restored.nodePrefix);
    assertTrue(restored.updateInProgress);
    assertEquals(4, restored.sequenceNumber);
  }
}
