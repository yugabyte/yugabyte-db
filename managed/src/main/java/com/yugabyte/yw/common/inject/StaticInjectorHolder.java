/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.inject;

import com.google.inject.Inject;
import java.util.concurrent.atomic.AtomicReference;
import play.inject.Injector;

/**
 * This is a dirty hack for migrating from Play 2.6, which allows is to get Injector anywhere. As
 * Play 2.8 does not allow that, but refactoring all the code at once is too much - we create this
 * static holder, which can be used in existing places instead of Play.app().injector()... DO NOT
 * USE THAT IN ANY OF THE NEW CODE YOU'RE WRITING!!! Use normal DI pipelines!!!
 */
@Deprecated
public class StaticInjectorHolder {

  private static final AtomicReference<Injector> injectorRef = new AtomicReference<>();

  @Inject
  public StaticInjectorHolder(Injector injector) {
    StaticInjectorHolder.injectorRef.set(injector);
  }

  public static Injector injector() {
    return injectorRef.get();
  }
}
