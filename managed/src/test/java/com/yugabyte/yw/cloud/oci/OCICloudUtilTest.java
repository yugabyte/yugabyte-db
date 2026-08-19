// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.cloud.oci;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.util.HashMap;
import java.util.Map;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class OCICloudUtilTest {

  @Test
  public void testComputeEffectiveTagCountWithOnlyMandatoryTags() {
    assertEquals(7, OCICloudUtil.computeEffectiveTagCount(Map.of()));
  }

  @Test
  public void testComputeEffectiveTagCountDedupesOverlappingUserTags() {
    Map<String, String> userTags =
        Map.of(
            "Name", "custom-name",
            "node-uuid", "custom-node",
            "env", "prod");
    assertEquals(8, OCICloudUtil.computeEffectiveTagCount(userTags));
  }

  @Test
  public void testGetTagCountValidationErrorWithinLimit() {
    Map<String, String> userTags = Map.of("tag1", "v1", "tag2", "v2", "tag3", "v3");
    assertNull(OCICloudUtil.getTagCountValidationError(userTags));
  }

  @Test
  public void testGetTagCountValidationErrorExceedsLimit() {
    Map<String, String> userTags = new HashMap<>();
    for (int i = 0; i < 4; i++) {
      userTags.put("tag" + i, "value" + i);
    }
    String error = OCICloudUtil.getTagCountValidationError(userTags);
    assertTrue(error.contains("provisioning would apply 11 tags"));
    assertTrue(
        error.contains(
            "Name, customer-uuid, node-uuid, tag0, tag1, tag2, tag3, universe-uuid,"
                + " yb-server-type, yb_user_email, yb_yba_url"));
    assertTrue(error.contains("Reduce the number of instance tags by at least 1"));
  }

  @Parameters({"3, null", "4, exceeds"})
  @Test
  public void testTagCountBoundary(int userTagCount, String expected) {
    Map<String, String> userTags = new HashMap<>();
    for (int i = 0; i < userTagCount; i++) {
      userTags.put("tag" + i, "value" + i);
    }
    String error = OCICloudUtil.getTagCountValidationError(userTags);
    if ("null".equals(expected)) {
      assertNull(error);
    } else {
      assertTrue(error.contains("provisioning would apply"));
    }
  }
}
