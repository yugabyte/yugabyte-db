// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.annotations;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;
import play.mvc.With;

/**
 * Blocks an endpoint that changes a provider (or one of its sub resources) when the provider is
 * created and managed by YNP, unless the call comes from YNP itself.
 *
 * <p>Only applies to endpoints whose path contains the provider UUID - {@code
 * /providers/<providerUUID>/...}. Endpoints reached through another resource have to call {@link
 * com.yugabyte.yw.common.YnpProviderUtil#checkYnpManagedProvider} directly.
 */
@With(BlockYnpManagedProviderHandler.class)
@Target(ElementType.METHOD)
@Retention(RetentionPolicy.RUNTIME)
public @interface BlockYnpManagedProvider {
  /** Description of the blocked change, used in the error message. */
  String operation() default "This operation";
}
