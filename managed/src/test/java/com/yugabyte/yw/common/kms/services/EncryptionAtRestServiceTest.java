/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.services;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.algorithms.SupportedAlgorithmInterface;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

enum TestAlgorithm implements SupportedAlgorithmInterface {
  TEST_ALGORITHM(Arrays.asList(1, 2, 3, 4));

  private List<Integer> keySizes;

  public List<Integer> getKeySizes() {
    return this.keySizes;
  }

  private TestAlgorithm(List<Integer> keySizes) {
    this.keySizes = keySizes;
  }
}

class TestEncryptionAtRestService extends EncryptionAtRestService<TestAlgorithm> {
  public TestEncryptionAtRestService(
      ApiHelper apiHelper,
      KeyProvider keyProvider,
      EncryptionAtRestManager util,
      boolean createRequest) {
    super(keyProvider);
    this.createRequest = createRequest;
  }

  public TestEncryptionAtRestService(
      ApiHelper apiHelper, KeyProvider keyProvider, EncryptionAtRestManager util) {
    this(apiHelper, keyProvider, util, false);
  }

  public boolean createRequest;

  @Override
  protected TestAlgorithm[] getSupportedAlgorithms() {
    return TestAlgorithm.values();
  }

  @Override
  protected byte[] createKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    return "some_key_id".getBytes();
  }

  @Override
  protected byte[] rotateKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    return "some_key_id".getBytes();
  }

  @Override
  public byte[] retrieveKeyWithService(UUID configUUID, byte[] keyRef) {
    this.createRequest = !this.createRequest;
    return this.createRequest ? null : "some_key_value".getBytes();
  }

  @Override
  public byte[] validateRetrieveKeyWithService(
      UUID configUUID, byte[] keyRef, ObjectNode authConig) {
    this.createRequest = !this.createRequest;
    return this.createRequest ? null : "some_key_value".getBytes();
  }

  @Override
  public ObjectNode getKeyMetadata(UUID configUUID) {
    // Does nothing here because we test for individual KMS providers.
    return null;
  }

  @Override
  public byte[] encryptKeyWithService(UUID configUUID, byte[] universeKey) {
    return null;
  }

  @Override
  public void refreshKmsWithService(UUID configUUID, ObjectNode authConfig) throws Exception {
    // Does nothing here
    throw new UnsupportedOperationException("Unimplemented method 'refreshKmsWithService'");
  }
}

@RunWith(MockitoJUnitRunner.class)
public class EncryptionAtRestServiceTest extends FakeDBApplication {

  EncryptionAtRestConfig config;

  @Before
  public void setUp() {
    config = new EncryptionAtRestConfig();
  }

  @Test
  public void testGetServiceNotImplemented() {
    assertNull(new EncryptionAtRestManager(null).getServiceInstance("UNSUPPORTED"));
  }

  @Test
  public void testGetServiceNewInstance() {
    assertNotNull(new EncryptionAtRestManager(null).getServiceInstance("SMARTKEY"));
  }

  @Test
  public void testGetServiceSingleton() {
    EncryptionAtRestService newService =
        new EncryptionAtRestManager(null).getServiceInstance("SMARTKEY");
    assertEquals(KeyProvider.SMARTKEY.getServiceInstance().hashCode(), newService.hashCode());
  }

  @Test
  public void testCreateKey() {
    EncryptionAtRestService service =
        new TestEncryptionAtRestService(null, KeyProvider.AWS, mockEARManager, true);
    UUID customerUUID = UUID.randomUUID();
    UUID universeUUID = UUID.randomUUID();
    service.createAuthConfig(
        customerUUID, "some_config_name", Json.newObject().put("some_key", "some_value"));
    EncryptionAtRestConfig testConfig = config.clone();
    byte[] key = service.createKey(universeUUID, customerUUID, testConfig);
    assertNotNull(key);
    assertEquals("some_key_id", new String(key));
  }
}
