// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;

import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class AWSUtilTest extends FakeDBApplication {
  @Before
  public void setup() {
    when(mockAWSUtil.isHostBaseS3Standard(anyString())).thenCallRealMethod();
  }

  @Test
  @Parameters({
    // Standard URLs
    "s3.ap-south-1.amazonaws.com, true",
    "s3.amazonaws.com, true",
    "s3-us-east-1.amazonaws.com, true",
    "s3.us-east-1.amazonaws.com, true",
    "s3-control.me-central-1.amazonaws.com, true",
    "s3.dualstack.mx-central-1.amazonaws.com, true",
    "s3-website-us-west-2.amazonaws.com, true",
    // Standard should always end with '.amazonaws.com'
    "s3-amazonaws.com, false",
    // Any random URL - rejected
    "rmn.kiba.local, false",
    // Custom private host bases (standards will start with 's3.')
    "bucket.vpce-abcxyz-lsuyzz63.s3.ap-south-1.vpce.amazonaws.com, false",
    "bucket.vpce-a1.s3.ap-south-1.amazonaws.com, false",
    "bucket.vpce.amazonaws.com, false"
  })
  public void testS3HostBase(String hostBase, boolean expectedResult) {
    boolean actualResult = mockAWSUtil.isHostBaseS3Standard(hostBase);
    assertEquals(actualResult, expectedResult);
  }
}
