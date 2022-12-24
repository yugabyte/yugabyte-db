package com.yugabyte.yw.common.config;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.config.ConfDataType.parseBoolean;
import static com.yugabyte.yw.common.config.ConfDataType.parseStrList;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.junit.Test;

public class ConfDataTypeTest {
  @Test
  public void boolParse() {
    assertEquals(true, parseBoolean("TRUE"));
    assertEquals(true, parseBoolean("true"));
    assertEquals(false, parseBoolean("False"));
    assertEquals(false, parseBoolean("FaLsE"));
    assertPlatformException(() -> parseBoolean(" A BCD  "));
    assertPlatformException(() -> parseBoolean("Random_String"));
  }

  @Test
  public void strListParse() {
    List<String> list = new ArrayList<>(Arrays.asList("Three", "Sample", "Strings"));
    assertTrue(list.equals(parseStrList("[\"Three\",\"Sample\",\"Strings\"]")));
    // Strings should be enclosed within double quotes
    assertPlatformException(() -> parseStrList("[Three,Sample,String]"));
    assertPlatformException(() -> parseStrList("Random string"));
  }
}
