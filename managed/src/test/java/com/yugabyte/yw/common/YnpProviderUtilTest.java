// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class YnpProviderUtilTest {

  // As add_result in YNP's precheck script writes it: passed checks first.
  private static final String OUTPUT =
      "{\n"
          + "\"results\":[\n"
          + "    {\n"
          + "      \"check\": \"Sudo Access\",\n"
          + "      \"result\": \"PASS\",\n"
          + "      \"message\": \"User has sudo access\"\n"
          + "    },\n"
          + "    {\n"
          + "      \"check\": \"FIPS Mode\",\n"
          + "      \"result\": \"FAIL\",\n"
          + "      \"message\": \"Kernel FIPS mode is off and no reboot is pending\"\n"
          + "    }\n"
          + "]}\n";

  @Test
  public void testYnpPreflightFailureMessageNamesTheFailedChecks() {
    assertEquals(
        "Failed YNP preflight checks for node n1: FIPS Mode: Kernel FIPS mode is off and no"
            + " reboot is pending",
        YnpProviderUtil.ynpPreflightFailureMessage("n1", 1, OUTPUT));
  }

  @Test
  public void testYnpPreflightFailureMessageWithoutResults() {
    assertEquals(
        "Failed YNP preflight checks for node n1 with exit code 2. Output: bash: not found",
        YnpProviderUtil.ynpPreflightFailureMessage("n1", 2, "bash: not found"));
  }
}
