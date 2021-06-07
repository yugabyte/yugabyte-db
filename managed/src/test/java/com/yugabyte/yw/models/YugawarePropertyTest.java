// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

public class YugawarePropertyTest extends FakeDBApplication {

  @Before
  public void setUp() {
    YugawareProperty.addConfigProperty(
        "existing-config", ImmutableMap.of("foo", "bar"), "Existing Config");
  }

  @Test
  public void testAddNewProperty() {
    YugawareProperty.addConfigProperty(
        "Foo", ImmutableMap.of("foo", "bar"), "Sample data");
    assertEquals(2, YugawareProperty.find.query().findCount());
  }

  @Test
  public void testAddExistingProperty() {
    YugawareProperty.addConfigProperty(
        "existing-config", ImmutableMap.of("foo1", "bar1"), "Sample data");
    assertEquals(1, YugawareProperty.find.query().findCount());
  }

  @Test
  public void testGetProperty() {
    YugawareProperty p = YugawareProperty.get("existing-config");
    assertNotNull(p);
    assertEquals(p.getValue().get("foo"), "bar");
  }

  @Test
  public void testGetUnknownProperty() {
    YugawareProperty p = YugawareProperty.get("unknown-config");
    assertNull(p);
  }

  @Test
  public void testUpdateExistingProperty() {
    YugawareProperty.addConfigProperty(
        "existing-config", ImmutableMap.of("foo", "bar2"), "Sample data");
    YugawareProperty property = YugawareProperty.get("existing-config");
    assertEquals(property.getValue().get("foo"), "bar2");
  }
}
