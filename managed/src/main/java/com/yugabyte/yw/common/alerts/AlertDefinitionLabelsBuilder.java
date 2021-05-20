/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import com.yugabyte.yw.models.AlertDefinitionLabel;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;

import java.util.ArrayList;
import java.util.List;

public class AlertDefinitionLabelsBuilder {
  private List<AlertDefinitionLabel> labels = new ArrayList<>();

  public static AlertDefinitionLabelsBuilder create() {
    return new AlertDefinitionLabelsBuilder();
  }

  public AlertDefinitionLabelsBuilder appendUniverse(Universe universe) {
    labels.add(
        new AlertDefinitionLabel(KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString()));
    labels.add(new AlertDefinitionLabel(KnownAlertLabels.UNIVERSE_NAME, universe.name));
    return this;
  }

  public List<AlertDefinitionLabel> get() {
    return labels;
  }
}
