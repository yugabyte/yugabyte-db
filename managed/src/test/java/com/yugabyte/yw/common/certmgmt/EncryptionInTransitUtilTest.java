/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.certmgmt;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class EncryptionInTransitUtilTest {
  protected static final Logger LOG = LoggerFactory.getLogger(EncryptionInTransitUtilTest.class);

  @Before
  public void setup() {}

  @Test
  public void testMaskConfigDataHCVault() throws Exception {

    String data = "s.fj4WtEMQ1MV1fnxQkpXA9ytV";

    LOG.debug("Input is:: {}", data);

    try {
      UUID uid = UUID.fromString("f33e3c9b-75ab-4c30-80ad-cba85646ea39");

      String encryptedObj = EncryptionInTransitUtil.maskCertConfigData(uid, data);
      assertNotNull(encryptedObj);

      LOG.debug("Data:: {}", encryptedObj);
      String unencryptedObj = EncryptionInTransitUtil.unmaskCertConfigData(uid, encryptedObj);
      assertNotNull(unencryptedObj);

      assertEquals(data, unencryptedObj);
    } catch (Exception e) {
      LOG.error("test:: failed", e);
      throw e;
    }
  }
}
