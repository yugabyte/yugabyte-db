// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.util.Collections;
import java.util.Date;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;

/** PR1 stub - real implementation lands in a follow-up component PR. */
@Slf4j
@Singleton
class YBAComponent implements SupportBundleComponent {

  @Inject
  YBAComponent() {}

  @Override
  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {}

  @Override
  public void downloadComponentBetweenDates(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    log.info("YBAComponent stub: skipping collection on universe '{}'", universe.getName());
  }

  @Override
  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    return Collections.emptyMap();
  }
}
