/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.kms.util.hashicorpvault;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.RETURNS_DEEP_STUBS;
import static org.mockito.Mockito.mock;
// import static org.mockito.Mockito.spy;
// import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.bettercloud.vault.Vault;
import com.bettercloud.vault.rest.RestResponse;
import com.yugabyte.yw.common.FakeDBApplication;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(MockitoJUnitRunner.class)
public class VaultAccessorTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(VaultAccessor.class);

  boolean MOCK_RUN;

  String vaultAddr, vaultToken;
  VaultAccessor hcVaultAccessor1, hcVaultAccessor2;

  @Before
  public void setUp() {
    MOCK_RUN = VaultEARServiceUtilTest.MOCK_RUN;

    vaultAddr = VaultEARServiceUtilTest.vaultAddr;
    vaultToken = VaultEARServiceUtilTest.vaultToken;

    try {
      if (MOCK_RUN) {
        hcVaultAccessor1 = mock(VaultAccessor.class); // with vaultToken
        hcVaultAccessor2 = mock(VaultAccessor.class); // with vaultTokenWithTTL

        when(hcVaultAccessor1.getMountType("qa_transit/")).thenReturn("TRANSIT");
        when(hcVaultAccessor1.readAt(anyString(), anyString())).thenReturn("1");
        when(hcVaultAccessor1.getTokenExpiryFromVault()).thenReturn(Arrays.asList(0L, 200L));

        // @SuppressWarnings("unused")
        // when(hcVaultAccessor1.createToken(anyString())).thenReturn("newToken");
        when(hcVaultAccessor2.getTokenExpiryFromVault()).thenReturn(Arrays.asList(60L, 200L));
      }
    } catch (Exception e) {
      assertTrue(false);
    }
  }

  void getVaultObjects() throws Exception {
    // change condition to test with actual setup
    hcVaultAccessor1 = VaultAccessor.buildVaultAccessor(vaultAddr, vaultToken);

    long configTTL2 = 3600 * VaultAccessor.TTL_RENEWAL_BEFORE_EXPIRY_HRS; // in seconds
    String newToken2 = hcVaultAccessor1.createToken(String.valueOf(configTTL2) + "s");
    hcVaultAccessor2 = VaultAccessor.buildVaultAccessor(vaultAddr, newToken2);
  }

  @Test
  public void testHCVaultAccessorObject() {

    try {

      if (!MOCK_RUN) getVaultObjects();
      {
        String path = "qa_transit/";
        String returnPath = hcVaultAccessor1.getMountType(path);
        assertEquals("TRANSIT", returnPath);
      }

      {
        String path = "qa_transit/keys/key1";
        String result = hcVaultAccessor1.readAt(path, "latest_version");
        LOG.debug("latest version of key is {}", result);
        assertNotEquals("", result);
      }

      {
        List<Object> list = hcVaultAccessor1.getTokenExpiryFromVault();
        long ttl = (long) list.get(0);
        long expiry = (long) list.get(1);
        assertEquals(0, ttl);
        assertNotEquals(0, expiry);
      }

    } catch (Exception exception) {
      LOG.error("Exception occured :" + exception);
      assertTrue(false);
    }
  }

  @Test
  public void testHCVaultAccessorTTL() {
    try {

      if (!MOCK_RUN) getVaultObjects();

      {
        // token never expires
        List<Object> list = hcVaultAccessor1.getTokenExpiryFromVault();
        long ttl = (long) list.get(0);
        long expiry = (long) list.get(1);
        assertEquals(0, ttl);
        assertNotEquals(0, expiry);
      }

      {
        // token have specific expiry set
        List<Object> list = hcVaultAccessor2.getTokenExpiryFromVault();
        long ttl = (long) list.get(0);
        long expiry = (long) list.get(1);
        LOG.info("TTL is {} and Expiry is {} ({})", ttl, expiry, (new Date(expiry)).toString());
        assertNotEquals(0, ttl);
        assertNotEquals(0, expiry);
      }

    } catch (Exception exception) {
      LOG.error("Exception occured :" + exception);
      assertTrue(false);
    }
  }

  @Test
  public void testCheckForResponseFailureUsingMock() throws Exception {

    if (!MOCK_RUN) return;

    VaultAccessor vAccessor;
    Vault v = mock(Vault.class);
    // VaultConfig c = mock(VaultConfig.class);
    vAccessor = new VaultAccessor(v, 1);

    /*
    VaultAccessor spyVaultAccessor = spy(vAccessor);
    try {
    doNothing().when(spyVaultAccessor).tokenSelfLookupCheck();
    doNothing().when(spyVaultAccessor).getTokenExpiryFromVault();
    doNothing().when(spyVaultAccessor).renewSelf();
    when(spyVaultAccessor.checkForResponseFailure(any())).thenCallRealMethod();
    } catch(Exception e) {}
    */

    RestResponse restResp = mock(RestResponse.class);
    when(restResp.getStatus()).thenReturn(200).thenReturn(401);
    when(restResp.getBody()).thenReturn("test".getBytes());

    try {
      // we dont expect exception
      vAccessor.checkForResponseFailure(restResp);
    } catch (Exception e) {
      LOG.debug("FAILURE:: We should not receive exception ... FAILURE");
    }
    verify(restResp, times(1)).getStatus();
    verify(restResp, times(0)).getBody();

    try {
      // we should get exception
      vAccessor.checkForResponseFailure(restResp);
      LOG.debug("FAILURE:: We should have receive exception ... FAILURE");
    } catch (Exception e) {
    }
    verify(restResp, times(2)).getStatus();
    verify(restResp, times(1)).getBody();
  }

  @Test
  public void testRenewToken() throws Exception {

    // how to test
    // 1. create a new token with expiry 105 seconds
    // 2. after token creation sleep for 5 seconds
    // 3. note current token ttl (ttl1)
    //      => It should be around 90-99, but less than 100
    // 4. renew token
    // 5. note token ttl now (ttl2)
    //      => It should be around 100-104, and less than ttl1.

    VaultAccessor vAccessor;

    if (MOCK_RUN) {
      Vault v = mock(Vault.class, RETURNS_DEEP_STUBS);
      vAccessor = new VaultAccessor(v, 1);

      when(v.auth().lookupSelf().getTTL()).thenReturn(60L).thenReturn(100L);

      // when(vAccessor.getTokenExpiryFromVault())
      // .thenReturn(Arrays.asList(60L, 200L))
      // .thenReturn(Arrays.asList(100L, 200L));
    } else {
      long configTTL = 3600 * VaultAccessor.TTL_RENEWAL_BEFORE_EXPIRY_HRS; // in seconds
      VaultAccessor va = VaultAccessor.buildVaultAccessor(vaultAddr, vaultToken);
      String newToken = va.createToken(String.valueOf(configTTL) + "s");
      vAccessor = VaultAccessor.buildVaultAccessor(vaultAddr, newToken);
    }
    Thread.sleep(5000);
    long ttl1 = (long) vAccessor.getTokenExpiryFromVault().get(0);

    vAccessor.renewSelf();
    long ttl2 = (long) vAccessor.getTokenExpiryFromVault().get(0);

    LOG.debug("TTL vaules are " + ttl1 + " and " + ttl2);
    assertEquals(true, ttl1 < ttl2);
  }
}
