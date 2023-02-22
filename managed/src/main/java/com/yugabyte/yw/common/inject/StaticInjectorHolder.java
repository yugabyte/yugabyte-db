package com.yugabyte.yw.common.inject;

import play.inject.Injector;

/**
 * This is a dirty hack for migrating from Play 2.6, which allows is to get Injector anywhere. AS
 * Play 2.8 does not allow that, but refactoring all the code at once is too much - we create this
 * static holder, which can be used in existing places instead of Play.app().injector()... DO NOT
 * USE THAT IN ANY OF THE NEW CODE YOU'RE WRITING!!! Use normal DI pipelines!!!
 */
@Deprecated
public class StaticInjectorHolder {

  static Injector injector;

  public static Injector injector() {
    return injector;
  }
}
