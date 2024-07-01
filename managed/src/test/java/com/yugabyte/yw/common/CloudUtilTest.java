// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import junitparams.JUnitParamsRunner;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class CloudUtilTest extends FakeDBApplication {

  Customer testCustomer;
  Universe testUniverse;
  CustomerConfig s3WithProxyNoUsernamePassword, s3WithoutProxy;
  JsonNode s3FormDataWithProxyNoUsernamePasswordJson =
      Json.parse(
          "{\"configName\": \""
              + "test-S3_1"
              + "\", \"name\": \"S3\","
              + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
              + " \"s3://test/\","
              + " \"AWS_ACCESS_KEY_ID\": \"A-KEY\", \"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\","
              + " \"PROXY_SETTINGS\": {\"PROXY_HOST\": \"1.2.3.4\", \"PROXY_PORT\": 3128}}}");

  JsonNode s3FormDataWithoutProxyJson =
      Json.parse(
          "{\"configName\": \""
              + "test-S3_2"
              + "\", \"name\": \"S3\","
              + " \"type\": \"STORAGE\", \"data\": {\"BACKUP_LOCATION\":"
              + " \"s3://test\","
              + " \"AWS_ACCESS_KEY_ID\": \"A-KEY\", \"AWS_SECRET_ACCESS_KEY\": \"A-SECRET\"}}");

  @Before
  public void setup() {
    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse(testCustomer.getId());
    when(mockCloudUtilFactory.getCloudUtil(eq("S3"))).thenReturn(mockAWSUtil);
    when(mockAWSUtil.createYbcProxyConfig(any(), any())).thenCallRealMethod();
    when(mockAWSUtil.getOldProxySpec(any())).thenCallRealMethod();
    s3WithProxyNoUsernamePassword =
        CustomerConfig.createWithFormData(
            testCustomer.getUuid(), s3FormDataWithProxyNoUsernamePasswordJson);
    s3WithoutProxy =
        CustomerConfig.createWithFormData(testCustomer.getUuid(), s3FormDataWithoutProxyJson);
  }

  @Test
  public void testOldProxyWithoutProxyDefined() {
    org.yb.ybc.ProxyConfig proxyConfig =
        mockCloudUtilFactory
            .getCloudUtil(Util.S3)
            .createYbcProxyConfig(testUniverse, s3WithoutProxy.getDataObject());
    assertNull(proxyConfig);
  }

  @Test
  public void testOldProxyWithProxy() {
    org.yb.ybc.ProxyConfig proxyConfig =
        mockCloudUtilFactory
            .getCloudUtil(Util.S3)
            .createYbcProxyConfig(testUniverse, s3WithProxyNoUsernamePassword.getDataObject());
    assertNotNull(proxyConfig);
    assertTrue(proxyConfig.getDefaultProxy().getHost().equals("1.2.3.4"));
  }
}
