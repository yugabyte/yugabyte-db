package com.yugabyte.yw.common.inject;

import com.google.inject.Inject;
import play.inject.Injector;

public class StaticInjectorSetter {

  @Inject
  StaticInjectorSetter(Injector injector) {
    StaticInjectorHolder.injector = injector;
  }
}
