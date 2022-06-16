/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.MatcherAssert.assertThat;

import org.apache.commons.lang3.StringUtils;
import org.junit.Test;

public class ShellResponseTest {

  @Test
  public void emptyOutputTest() {
    ShellResponse shellResponse = ShellResponse.create(0, "Command output:");

    String output = shellResponse.extractRunCommandOutput();

    assertThat(output, equalTo(StringUtils.EMPTY));
  }

  @Test
  public void nonEmptyOutputTest() {
    ShellResponse shellResponse = ShellResponse.create(0, "Command output:\n Some output");

    String output = shellResponse.extractRunCommandOutput();

    assertThat(output, equalTo("Some output"));
  }

  @Test
  public void wrongOutputTest() {
    ShellResponse shellResponse = ShellResponse.create(0, "BlaBlaBla");

    assertThat(shellResponse::extractRunCommandOutput, thrown(RuntimeException.class));
  }
}
