/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.templates;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.MatcherAssert.assertThat;

import org.junit.Test;

public class PlaceholderSubstitutorTest {

  @Test
  public void testComplexTemplateSubstitution() {
    String template =
        "some {{ template {{ to_replace }} with multiple {{ suffixes"
            + " {{ and {{ prefixes {{ to_replace }} }} }} ";
    PlaceholderSubstitutor placeholderSubstitutor =
        new PlaceholderSubstitutor(
            key -> {
              if (key.equals("to_replace")) {
                return "replaced";
              }
              return null;
            });

    String replaced = placeholderSubstitutor.replace(template);

    assertThat(
        replaced,
        equalTo(
            "some {{ template replaced with multiple {{ suffixes"
                + " {{ and {{ prefixes replaced }} }} "));
  }
}
