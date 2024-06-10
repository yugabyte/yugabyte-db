package com.yugabyte.yw.common.kms.util;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AwsEARServiceUtilTest extends FakeDBApplication {
  @Test
  public void testGetPolicyBase() {
    assertNotNull(AwsEARServiceUtil.getPolicyBase());
  }

  @Test
  public void testBindParamsToPolicy() {
    String userArn = "test_user_arn";
    String rootArn = "test_root_arn";
    ObjectNode policy =
        AwsEARServiceUtil.bindParamsToPolicyBase(
            AwsEARServiceUtil.getPolicyBase(), userArn, rootArn);
    ArrayNode statements = (ArrayNode) policy.get("Statement");
    assertEquals(statements.get(0).get("Principal").get("AWS").asText(), rootArn);
    assertEquals(statements.get(1).get("Principal").get("AWS").asText(), userArn);
  }
}
