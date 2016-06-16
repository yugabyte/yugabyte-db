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

  // The devops home.
  public static String getDevopsHome() {
    String ybDevopsHome = System.getProperty("yb.devops.home");
    if (ybDevopsHome == null) {
      LOG.error("Devops repo path not found. Please specify yb.devops.home property: " +
                "'sbt run -Dyb.devops.home=<path to devops repo>'");
      throw new RuntimeException("Property yb.devops.home was not found.");
    }
    return ybDevopsHome;
  }
}
