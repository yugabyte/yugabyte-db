package com.yugabyte.yw.common.supportbundle;

import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.util.Date;

public interface SupportBundleComponent {

  void downloadComponent(Customer customer, Universe universe, Path bundlePath, NodeDetails node)
      throws Exception;

  void downloadComponentBetweenDates(
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception;
}
