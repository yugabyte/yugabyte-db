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

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.kms.algorithms.SmartKeyAlgorithm;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import java.util.Base64;
import java.util.Map;
import java.util.UUID;
import play.libs.Json;

/**
 * An implementation of EncryptionAtRestService to communicate with Equinix SmartKey
 * https://support.smartkey.io/api/index.html
 */
public class SmartKeyEARService extends EncryptionAtRestService<SmartKeyAlgorithm> {

  private final ApiHelper apiHelper;
  private final RuntimeConfGetter confGetter;

  public SmartKeyEARService(RuntimeConfGetter confGetter) {
    super(KeyProvider.SMARTKEY);
    this.apiHelper = StaticInjectorHolder.injector().instanceOf(ApiHelper.class);
    this.confGetter = confGetter;
  }

  /**
   * A method to retrieve a SmartKey API session token from the inputted api token
   *
   * @param authConfig
   * @return a session token to be used to authorize subsequent requests
   */
  public String retrieveSessionAuthorization(ObjectNode authConfig) {
    final String endpoint = "/sys/v1/session/auth";
    final String apiToken = authConfig.get("api_key").asText();
    final String baseUrl = authConfig.get("base_url").asText();
    final Map<String, String> headers =
        ImmutableMap.of("Authorization", String.format("Basic %s", apiToken));
    final String url = Util.buildURL(baseUrl, endpoint);
    final JsonNode response = this.apiHelper.postRequest(url, Json.newObject(), headers);
    final JsonNode errors = response.get("error");
    if (errors != null) throw new RuntimeException(errors.toString());
    return String.format("Bearer %s", response.get("access_token").asText());
  }

  @Override
  protected SmartKeyAlgorithm[] getSupportedAlgorithms() {
    return SmartKeyAlgorithm.values();
  }

  @Override
  protected byte[] createKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    final String algorithm = "AES";
    final int keySize = 256;
    final ObjectNode validateResult = validateEncryptionKeyParams(algorithm, keySize);
    if (!validateResult.get("result").asBoolean()) {
      final String errMsg =
          String.format(
              "Invalid encryption key parameters detected for create operation in "
                  + "universe %s: %s",
              universeUUID, validateResult.get("errors").asText());
      LOG.error(errMsg);
      throw new IllegalArgumentException(errMsg);
    }
    final String endpoint = "/crypto/v1/keys";
    final ArrayNode keyOps = Json.newArray().add("EXPORT").add("APPMANAGEABLE");
    ObjectNode payload =
        Json.newObject()
            .put("name", universeUUID.toString())
            .put("obj_type", algorithm)
            .put("key_size", keySize);
    payload.set("key_ops", keyOps);
    final ObjectNode authConfig = getAuthConfig(configUUID);
    final String sessionToken = retrieveSessionAuthorization(authConfig);
    final Map<String, String> headers =
        ImmutableMap.of("Authorization", sessionToken, "Content-Type", "application/json");
    final String baseUrl = authConfig.get("base_url").asText();
    final String url = Util.buildURL(baseUrl, endpoint);
    final JsonNode response = this.apiHelper.postRequest(url, payload, headers);
    final JsonNode errors = response.get("error");
    if (errors != null) throw new RuntimeException(errors.toString());
    final String kId = response.get("kid").asText();
    return kId.getBytes();
  }

  @Override
  protected byte[] rotateKeyWithService(
      UUID universeUUID, UUID configUUID, EncryptionAtRestConfig config) {
    final byte[] currentKey = retrieveKey(universeUUID, configUUID, config);
    if (currentKey == null || currentKey.length == 0) {
      final String errMsg =
          String.format(
              "Universe encryption key for universe %s does not exist", universeUUID.toString());
      LOG.error(errMsg);
      throw new IllegalArgumentException(errMsg);
    }
    final String algorithm = "AES";
    final int keySize = 256;
    final String endpoint = "/crypto/v1/keys/rekey";
    final ArrayNode keyOps = Json.newArray().add("EXPORT").add("APPMANAGEABLE");
    ObjectNode payload =
        Json.newObject()
            .put("name", universeUUID.toString())
            .put("obj_type", algorithm)
            .put("key_size", keySize);
    payload.set("key_ops", keyOps);
    final ObjectNode authConfig = getAuthConfig(configUUID);
    final String sessionToken = retrieveSessionAuthorization(authConfig);
    final Map<String, String> headers =
        ImmutableMap.of("Authorization", sessionToken, "Content-Type", "application/json");
    final String baseUrl = authConfig.get("base_url").asText();
    final String url = Util.buildURL(baseUrl, endpoint);
    final JsonNode response = this.apiHelper.postRequest(url, payload, headers);
    final JsonNode errors = response.get("error");
    if (errors != null) throw new RuntimeException(errors.toString());
    final String kId = response.get("kid").asText();
    return kId.getBytes();
  }

  @Override
  public byte[] retrieveKeyWithService(UUID configUUID, byte[] keyRef) {
    byte[] keyVal = null;
    final ObjectNode authConfig = getAuthConfig(configUUID);
    final String endpoint = String.format("/crypto/v1/keys/%s/export", new String(keyRef));
    final String sessionToken = retrieveSessionAuthorization(authConfig);
    final Map<String, String> headers = ImmutableMap.of("Authorization", sessionToken);
    final String baseUrl = authConfig.get("base_url").asText();
    final String url = Util.buildURL(baseUrl, endpoint);
    final JsonNode response = this.apiHelper.getRequest(url, headers);
    final JsonNode errors = response.get("error");
    if (errors != null) throw new RuntimeException(errors.toString());
    keyVal = Base64.getDecoder().decode(response.get("value").asText());
    return keyVal;
  }

  @Override
  public byte[] validateRetrieveKeyWithService(
      UUID configUUID, byte[] keyRef, ObjectNode authConfig) {
    byte[] keyVal = null;
    final String endpoint = String.format("/crypto/v1/keys/%s/export", new String(keyRef));
    final String sessionToken = retrieveSessionAuthorization(authConfig);
    final Map<String, String> headers = ImmutableMap.of("Authorization", sessionToken);
    final String baseUrl = authConfig.get("base_url").asText();
    final String url = Util.buildURL(baseUrl, endpoint);
    final JsonNode response = this.apiHelper.getRequest(url, headers);
    final JsonNode errors = response.get("error");
    if (errors != null) throw new RuntimeException(errors.toString());
    keyVal = Base64.getDecoder().decode(response.get("value").asText());
    return keyVal;
  }

  @Override
  public void refreshKmsWithService(UUID configUUID, ObjectNode authConfig) throws Exception {
    // Smart key is deprecated - will not be adding new features to it.
    throw new UnsupportedOperationException("Unimplemented method 'refreshKmsWithService'");
  }

  @Override
  public ObjectNode getKeyMetadata(UUID configUUID) {
    ObjectNode keyMetadata = new ObjectMapper().createObjectNode();

    // Add key_provider field.
    keyMetadata.put("key_provider", KeyProvider.SMARTKEY.name());
    return keyMetadata;
  }

  public byte[] encryptKeyWithService(UUID configUUID, byte[] universeKey) {
    // KMS is deprecated. No more new functionality.
    return null;
  }
}
