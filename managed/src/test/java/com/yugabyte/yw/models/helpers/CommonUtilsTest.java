// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.junit.Test;
import play.libs.Json;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

public class CommonUtilsTest {

  @Test
  public void maskConfigWithSensitiveData() {
    ObjectNode config = Json.newObject();
    config.put("SOME_KEY", "SENSITIVE_DATA");
    config.put("KEY_DATA", "SENSITIVE_DATA");
    config.put("MY_KEY_DATA", "SENSITIVE_DATA");
    config.put("SOME_SECRET", "SENSITIVE_DATA");
    config.put("SECRET_DATA", "SENSITIVE_DATA");
    config.put("MY_SECRET_DATA", "SENSITIVE_DATA");
    JsonNode maskedData = CommonUtils.maskConfig(config);
    assertValue(maskedData, "SOME_KEY", "SE**********TA");
    assertValue(maskedData, "KEY_DATA", "SE**********TA");
    assertValue(maskedData, "MY_KEY_DATA", "SE**********TA");
    assertValue(maskedData, "SOME_SECRET", "SE**********TA");
    assertValue(maskedData, "SECRET_DATA", "SE**********TA");
    assertValue(maskedData, "MY_SECRET_DATA", "SE**********TA");
  }

  @Test
  public void testMaskConfigWithoutSensitiveData() {
    ObjectNode config = Json.newObject();
    config.put("SOME_DATA", "VISIBLE_DATA");
    JsonNode maskedData = CommonUtils.maskConfig(config);
    assertValue(maskedData, "SOME_DATA", "VISIBLE_DATA");
  }

  @Test
  public void testMaskConfigWithNullData() {
    JsonNode maskedData = CommonUtils.maskConfig(null);
    assertEquals(0, maskedData.size());
    assertNotNull(maskedData);
  }
}
