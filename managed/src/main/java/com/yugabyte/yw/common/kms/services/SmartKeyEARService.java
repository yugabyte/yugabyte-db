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
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import play.api.Play;
import play.libs.Json;

/**
 * The different encryption algorithms allowed and a mapping of
 * encryption algorithm -> valid encryption key sizes (bits)
 */
enum SmartKeyAlgorithm implements SupportedAlgorithmInterface {
    AES(Arrays.asList(128, 192, 256));

    private List<Integer> keySizes;

    public List<Integer> getKeySizes() {
        return this.keySizes;
    }

    private SmartKeyAlgorithm(List<Integer> keySizes) {
        this.keySizes = keySizes;
    }
}

/**
 * An implementation of EncryptionAtRestService to communicate with Equinix SmartKey
 * https://support.smartkey.io/api/index.html
 */
public class SmartKeyEARService extends EncryptionAtRestService<SmartKeyAlgorithm> {
    /**
     * To be used to make requests against SmartKey APIs
     */
    private ApiHelper apiHelper;

    /**
     * Constructor
     *
     * @param apiHelper is a library to make requests against a third party encryption key provider
     * @param keyProvider is a String representation of "SMARTKEY" (if it is valid in this context)
     */
    public SmartKeyEARService() {
        super(KeyProvider.SMARTKEY);
        this.apiHelper = Play.current().injector().instanceOf(ApiHelper.class);
    }

    /**
     * A method to retrieve a SmartKey API session token from the inputted api token
     *
     * @param customerUUID is the customer that the authentication configuration should
     *                     be retrieved for
     * @return a session token to be used to authorize subsequent requests
     */
    private String retrieveSessionAuthorization(ObjectNode authConfig) {
        final String endpoint = "/sys/v1/session/auth";
        final String apiToken = authConfig.get("api_key").asText();
        final String baseUrl = authConfig.get("base_url").asText();
        final Map<String, String> headers = ImmutableMap.of(
                "Authorization", String.format("Basic %s", apiToken)
        );
        final String url = Util.buildURL(baseUrl, endpoint);
        final JsonNode response = this.apiHelper.postRequest(url, null, headers);
        final JsonNode errors = response.get("error");
        if (errors != null) throw new RuntimeException(errors.toString());
        return String.format(
                "Bearer %s",
                response.get("access_token").asText()
        );
    }

    @Override
    protected SmartKeyAlgorithm[] getSupportedAlgorithms() { return SmartKeyAlgorithm.values(); }

    @Override
    protected byte[] createKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    ) {
        final String algorithm = config.get("algorithm");
        final int keySize = Integer.parseInt(config.get("key_size"));
        final ObjectNode validateResult = validateEncryptionKeyParams(algorithm, keySize);
        if (!validateResult.get("result").asBoolean()) {
            final String errMsg = String.format(
                    "Invalid encryption key parameters detected for create operation in " +
                            "universe %s: %s",
                    universeUUID,
                    validateResult.get("errors").asText()
            );
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        final String endpoint = "/crypto/v1/keys";
        final ArrayNode keyOps = Json.newArray()
                .add("EXPORT")
                .add("APPMANAGEABLE");
        ObjectNode payload = Json.newObject()
                .put("name", universeUUID.toString())
                .put("obj_type", algorithm)
                .put("key_size", keySize);
        payload.set("key_ops", keyOps);
        final ObjectNode authConfig = getAuthConfig(customerUUID);
        final String sessionToken = retrieveSessionAuthorization(authConfig);
        final Map<String, String> headers = ImmutableMap.of(
                "Authorization", sessionToken,
                "Content-Type", "application/json"
        );
        final String baseUrl = authConfig.get("base_url").asText();
        final String url = Util.buildURL(baseUrl, endpoint);
        final JsonNode response = this.apiHelper.postRequest(url, payload, headers);
        final JsonNode errors = response.get("error");
        if (errors != null) throw new RuntimeException(errors.toString());
        final String kId = response.get("kid").asText();
        byte[] ref = kId.getBytes();
        if (ref != null && ref.length > 0) addKeyRef(customerUUID, universeUUID, ref);
        return ref;
    }

    @Override
    protected byte[] rotateKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        final byte[] currentKey = retrieveKey(customerUUID, universeUUID);
        if (currentKey == null || currentKey.length == 0) {
            final String errMsg = String.format(
                    "Universe encryption key for customer %s in universe %s does not exist",
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        final String algorithm = config.get("algorithm");
        final int keySize = Integer.parseInt(config.get("key_size"));
        final String endpoint = "/crypto/v1/keys/rekey";
        final ArrayNode keyOps = Json.newArray()
                .add("EXPORT")
                .add("APPMANAGEABLE");
        ObjectNode payload = Json.newObject()
                .put("name", universeUUID.toString())
                .put("obj_type", algorithm)
                .put("key_size", keySize);
        payload.set("key_ops", keyOps);
        final ObjectNode authConfig = getAuthConfig(customerUUID);
        final String sessionToken = retrieveSessionAuthorization(authConfig);
        final Map<String, String> headers = ImmutableMap.of(
                "Authorization", sessionToken,
                "Content-Type", "application/json"
        );
        final String baseUrl = authConfig.get("base_url").asText();
        final String url = Util.buildURL(baseUrl, endpoint);
        final JsonNode response = this.apiHelper.postRequest(url, payload, headers);
        final JsonNode errors = response.get("error");
        if (errors != null) throw new RuntimeException(errors.toString());
        final String kId = response.get("kid").asText();
        return kId.getBytes();
    }

    @Override
    public byte[] retrieveKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            byte[] keyRef,
            Map<String, String> config
    ) {
        byte[] keyVal = null;
        final ObjectNode authConfig = getAuthConfig(customerUUID);
        final String endpoint = String.format("/crypto/v1/keys/%s/export", new String(keyRef));
        final String sessionToken = retrieveSessionAuthorization(authConfig);
        final Map<String, String> headers = ImmutableMap.of(
                "Authorization", sessionToken
        );
        final String baseUrl = authConfig.get("base_url").asText();
        final String url = Util.buildURL(baseUrl, endpoint);
        final JsonNode response = this.apiHelper.getRequest(url, headers);
        final JsonNode errors = response.get("error");
        if (errors != null) throw new RuntimeException(errors.toString());
        keyVal = Base64.getDecoder().decode(response.get("value").asText());
        // Update cache entry
        if (keyVal != null) EncryptionAtRestUtil
                .setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
        return keyVal;
    }
}
