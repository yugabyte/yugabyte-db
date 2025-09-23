/*
 * Copyright 2024 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.util.Date;
import lombok.AllArgsConstructor;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SupportBundleComponentDownload extends AbstractTaskBase {

  @AllArgsConstructor
  public static class Params extends AbstractTaskParams {
    final SupportBundleComponent supportBundleComponent;
    final SupportBundleTaskParams supportBundleTaskParams;
    final Customer customer;
    final Universe universe;
    final Path bundlePath;
    final NodeDetails node;
    final Date startDate, endDate;
  }

  @Override
  public SupportBundleComponentDownload.Params taskParams() {
    return (SupportBundleComponentDownload.Params) taskParams;
  }

  @Inject
  public SupportBundleComponentDownload(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    Params params = taskParams();
    try {
      // Call the downloadComponentBetweenDates() function for all components.
      // Each component verifies if the dates are required and calls downloadComponent().
      params.supportBundleComponent.downloadComponentBetweenDates(
          params.supportBundleTaskParams,
          params.customer,
          params.universe,
          params.bundlePath,
          params.startDate,
          params.endDate,
          params.node);
    } catch (Exception e) {
      // Log the error and continue with the rest of support bundle collection.
      log.error(
          "Error occurred in support bundle collection for component '{}' on {} node",
          taskParams().supportBundleComponent.getClass().getSimpleName(),
          (taskParams().node == null) ? "YBA" : taskParams().node.getNodeName());
      e.printStackTrace();
    }
  }
}
