// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.pgsql;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * Runs the annotated test method against the Postgres port instead of the YSQL Connection Manager
 * port, even when tests are otherwise configured to use the connection manager.
 *
 * <p>Unlike the {@code @SkipOn*} annotations, this does not skip the test: the Postgres port is
 * always available on a connection-manager cluster, so the test still runs (and is still covered) -
 * it just bypasses the connection manager. This is the right behavior whenever a test's
 * expectations don't hold through the connection manager (e.g. assumptions about physical
 * connections or per-connection backend state).
 *
 * <p>The mini-cluster for an annotated test is built with no connection manager at all (there is
 * no point running it when the test connects on the Postgres port), so the test gets a clean
 * cluster without conn-mgr holding any physical backends.
 *
 * <p>Has no effect when tests are already running against the Postgres port.
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.METHOD)
public @interface BypassConnMgr {
  /** Why this test must bypass the connection manager. */
  String reason();
}
