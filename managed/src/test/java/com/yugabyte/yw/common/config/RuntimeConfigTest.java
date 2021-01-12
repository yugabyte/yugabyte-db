package com.yugabyte.yw.common.config;

import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.when;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigException;
import com.yugabyte.yw.models.Customer;

import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;

@RunWith(JUnitParamsRunner.class)
public class RuntimeConfigTest {

  @Rule
  public MockitoRule rule = MockitoJUnit.rule();

  @Mock
  private Config config;

  private RuntimeConfig<Customer> rtConfig;

  @Before
  public void setUp() {
    rtConfig = new RuntimeConfig<>(config);

    when(config.getString("yb.test.parameterA")).thenReturn("parameterA-value");
    when(config.getString("yb.test.parameterB")).thenReturn("parameterB-value");
    when(config.getString("yb.test.missing"))
        .thenThrow(new ConfigException.Missing("yb.test.missing"));
  }

  @Test
  // @formatter:off
  @Parameters({
    "null, null",
    ",",
    "abcdef, abcdef",
    "{{yb.test.parameterA}}, parameterA-value",
    "{{ yb.test.parameterA }}, parameterA-value",
    "<{{ yb.test.parameterA }}>, <parameterA-value>",
    "{{yb.test.parameterA}}={{yb.test.parameterB}}, parameterA-value=parameterB-value",
    "{{yb.test.missing}},",
    "<{{yb.test.missing}}>, <>",
    "{{yb.test.parameterA, {{yb.test.parameterA"
  })
  // @formatter:on
  public void testApply(@Nullable String src, @Nullable String expectedResult) {
    assertEquals(expectedResult, rtConfig.apply(src));
  }
}
