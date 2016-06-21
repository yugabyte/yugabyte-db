// Copyright (c) Yugabyte, Inc.

package controllers.commissioner;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Common {

  public static final Logger LOG = LoggerFactory.getLogger(Common.class);

  // The various cloud types supported.
  public enum CloudType {
    aws,
    gcp,
    azu,
  }
}
