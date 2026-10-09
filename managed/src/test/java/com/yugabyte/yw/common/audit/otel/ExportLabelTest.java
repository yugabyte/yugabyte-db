// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.audit.otel;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.Sets;
import com.yugabyte.yw.common.SwamperHelper.LabelType;
import java.util.Arrays;
import java.util.Set;
import java.util.stream.Collectors;
import org.junit.Test;

public class ExportLabelTest {

  // Fails the build when a swamper label is added without deciding whether it is exported.
  @Test
  public void everySwamperLabelIsAlignedOrExplicitlyExcluded() {
    Set<LabelType> unaccounted =
        Sets.difference(
            Arrays.stream(LabelType.values()).collect(Collectors.toSet()),
            Sets.union(ExportLabel.alignedSwamperLabels(), ExportLabel.NOT_EXPORTED));
    assertTrue(
        "swamper labels neither exported nor explicitly excluded: " + unaccounted,
        unaccounted.isEmpty());
  }

  @Test
  public void alignedLabelsTakeTheirNameFromSwamper() {
    for (ExportLabel label : ExportLabel.values()) {
      LabelType swamperLabel = label.getSwamperLabel();
      if (swamperLabel == null) {
        continue;
      }
      assertEquals(swamperLabel.getLabelName(), label.getAttributeName());
    }
  }

  @Test
  public void exportPlaneOnlyLabelsHaveNoSwamperCounterpart() {
    Set<String> swamperNames =
        Arrays.stream(LabelType.values()).map(LabelType::getLabelName).collect(Collectors.toSet());
    for (ExportLabel label : ExportLabel.values()) {
      if (label.getSwamperLabel() != null) {
        continue;
      }
      assertTrue(
          label + " has no swamper label but reuses the name " + label.getAttributeName(),
          !swamperNames.contains(label.getAttributeName()));
    }
  }

  @Test
  public void legacyNamesArePrefixed() {
    for (ExportLabel label : ExportLabel.values()) {
      String legacy = label.getLegacyAttributeName();
      if (legacy == null) {
        continue;
      }
      assertTrue(label + " legacy name is not prefixed: " + legacy, legacy.startsWith("yugabyte."));
    }
  }

  @Test
  public void k8sUnavailableLabelsAreDeclared() {
    for (ExportLabel label : ExportLabel.K8S_UNAVAILABLE) {
      assertNotNull(label + " must be a swamper-aligned label", label.getSwamperLabel());
    }
  }
}
