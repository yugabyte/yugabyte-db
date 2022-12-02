package com.yugabyte.yw.modules;

import com.typesafe.config.Config;
import javax.inject.Inject;
import org.yb.perf_advisor.module.PerfAdvisorDB;
import play.Environment;

public class PerfAdvisorDBModule extends PerfAdvisorDB {

  @Inject
  public PerfAdvisorDBModule(Environment environment, Config config) {
    // We can't fully place the module in perf advisor codebase because Play framework
    // will look for constructor with Environment and Config as a parameters, but
    // we don't want perf advisor code to depend on play framework classes.
    super(config);
  }
}
