// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class AccessKeyTest extends FakeDBApplication {
  Provider defaultProvider;
  Provider anotherProvider;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(customer);
    anotherProvider = ModelFactory.gcpProvider(customer);
  }

  @Test
  public void testValidCreate() {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.privateKey = "/path/to/private.key";
    keyInfo.vaultFile = "/path/to/vault_file";
    keyInfo.vaultPasswordFile = "/path/to/vault_password";

    AccessKey ak = AccessKey.create(defaultProvider.getUuid(), "access-code1", keyInfo);
    assertNotNull(ak);
    assertThat(ak.getKeyCode(), allOf(notNullValue(), equalTo("access-code1")));
    assertThat(ak.getProviderUUID(), allOf(notNullValue(), equalTo(defaultProvider.getUuid())));
    JsonNode keyInfoJson = Json.toJson(ak.getKeyInfo());
    assertValue(keyInfoJson, "publicKey", "/path/to/public.key");
    assertValue(keyInfoJson, "privateKey", "/path/to/private.key");
    assertValue(keyInfoJson, "vaultPasswordFile", "/path/to/vault_password");
    assertValue(keyInfoJson, "vaultFile", "/path/to/vault_file");
  }

  @Test
  public void testCreateWithInvalidDetails() {
    AccessKey ak =
        AccessKey.create(defaultProvider.getUuid(), "access-code1", new AccessKey.KeyInfo());
    assertNotNull(ak);
    assertThat(ak.getKeyCode(), allOf(notNullValue(), equalTo("access-code1")));
    assertThat(ak.getProviderUUID(), allOf(notNullValue(), equalTo(defaultProvider.getUuid())));
    AccessKey.KeyInfo keyInfo = ak.getKeyInfo();

    assertNull(keyInfo.publicKey);
    assertNull(keyInfo.privateKey);
  }

  @Test
  public void testGetValidKeyCode() {
    AccessKey.create(defaultProvider.getUuid(), "access-code1", new AccessKey.KeyInfo());
    AccessKey ak = AccessKey.get(defaultProvider.getUuid(), "access-code1");
    assertNotNull(ak);
    assertThat(ak.getKeyCode(), allOf(notNullValue(), equalTo("access-code1")));
  }

  @Test
  public void testGetInvalidKeyCode() {
    AccessKey ak = AccessKey.get(defaultProvider.getUuid(), "access-code1");
    assertNull(ak);
  }

  @Test
  public void testGetAllWithValidKeyCodes() {
    AccessKey.create(defaultProvider.getUuid(), "access-code1", new AccessKey.KeyInfo());
    AccessKey.create(defaultProvider.getUuid(), "access-code2", new AccessKey.KeyInfo());
    AccessKey.create(anotherProvider.getUuid(), "access-code3", new AccessKey.KeyInfo());
    List<AccessKey> accessKeys = AccessKey.getAll(defaultProvider.getUuid());
    assertEquals(2, accessKeys.size());
  }

  @Test
  public void testGetAllWithNoKeyCodes() {
    AccessKey.create(anotherProvider.getUuid(), "access-code3", new AccessKey.KeyInfo());
    List<AccessKey> accessKeys = AccessKey.getAll(defaultProvider.getUuid());
    assertEquals(0, accessKeys.size());
  }

  @Test
  public void testToJson() {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.publicKey = "/path/to/public.key";
    keyInfo.privateKey = "/path/to/private.key";
    AccessKey accessKey = AccessKey.create(defaultProvider.getUuid(), "access-code", keyInfo);
    JsonNode accessKeyJson = Json.toJson(accessKey);
    assertNotNull(accessKeyJson.get("keyInfo"));
    assertNotNull(accessKeyJson.get("idKey"));
  }
}
