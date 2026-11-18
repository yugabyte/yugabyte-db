/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
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
import static org.junit.Assert.assertThrows;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.junit.Test;
import org.slf4j.LoggerFactory;

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

  @Test
  public void processErrorsLogsOutputOfLoggedCommand() {
    ShellResponse shellResponse = ShellResponse.create(1, "sentinel_output");

    assertThat(
        loggedProcessErrors(shellResponse),
        equalTo("Query failed. Code: 1. Output: sentinel_output"));
  }

  @Test
  public void processErrorsKeepsOutputOfUnloggedCommandOutOfTheLog() {
    ShellResponse shellResponse = ShellResponse.create(1, "sentinel_output");
    shellResponse.logCmdOutput = false;

    assertThat(
        loggedProcessErrors(shellResponse), equalTo("Query failed. Code: 1. Output: <not logged>"));
  }

  // Returns the log of processErrors, after checking that the exception still has the output.
  private static String loggedProcessErrors(ShellResponse shellResponse) {
    Logger logger = (Logger) LoggerFactory.getLogger(ShellResponse.class);
    ListAppender<ILoggingEvent> appender = new ListAppender<>();
    appender.start();
    logger.addAppender(appender);
    try {
      RuntimeException e =
          assertThrows(RuntimeException.class, () -> shellResponse.processErrors("Query failed"));
      assertThat(e.getMessage(), equalTo("Query failed. Code: 1. Output: sentinel_output"));
    } finally {
      logger.detachAppender(appender);
    }
    return appender.list.stream()
        .map(ILoggingEvent::getFormattedMessage)
        .collect(Collectors.joining("\n"));
  }
}
