package com.yugabyte.yw.common.supportbundle;

import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.Date;
import java.nio.file.Path;
import java.io.IOException;
import java.text.ParseException;

public interface SupportBundleComponent {

  void downloadComponent(Customer customer, Universe universe, Path bundlePath) throws IOException;

  void downloadComponentBetweenDates(
      Customer customer, Universe universe, Path bundlePath, Date startDate, Date endDate)
      throws IOException, ParseException;
}
