// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.models.CustomerConfig;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.StreamSupport;
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
     * Constructor
     *
     * @param apiHelper is a library to make requests against a third party encryption key provider
     * @param keyProvider is a String representation of "SMARTKEY" (if it is valid in this context)
     */
    public SmartKeyEARService(ApiHelper apiHelper, String keyProvider) {
        super(apiHelper, keyProvider);
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
    protected String createEncryptionKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            String algorithm,
            int keySize
    ) {
        final String endpoint = "/crypto/v1/keys";
        // TODO: Remove EXPORT option once master keys are in encrypted format in Yugabyte
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
        return response.get("kid").asText();
    }

    @Override
    protected String getEncryptionKeyWithService(String kId, UUID customerUUID) {
        final String endpoint = String.format("/crypto/v1/keys/%s/export", kId);
        final ObjectNode authConfig = getAuthConfig(customerUUID);
        final String sessionToken = retrieveSessionAuthorization(authConfig);
        final Map<String, String> headers = ImmutableMap.of(
                "Authorization", sessionToken
        );
        final String baseUrl = authConfig.get("base_url").asText();
        final String url = Util.buildURL(baseUrl, endpoint);
        final JsonNode response = this.apiHelper.getRequest(url, headers);
        final JsonNode errors = response.get("error");
        if (errors != null) throw new RuntimeException(errors.toString());
        return response.get("value").asText();
    }

    @Override
    public String recoverEncryptionKeyWithService(UUID customerUUID, UUID universeUUID) {
        final String endpoint = "/crypto/v1/keys";
        final Map<String, String> queryParams = ImmutableMap.of(
                "name", universeUUID.toString(),
                "limit", "1"
        );
        final ObjectNode authConfig = getAuthConfig(customerUUID);
        final String sessionToken = retrieveSessionAuthorization(authConfig);
        final Map<String, String> headers = ImmutableMap.of(
                "Authorization", sessionToken
        );
        final String baseUrl = authConfig.get("base_url").asText();
        final String url = Util.buildURL(baseUrl, endpoint);
        final JsonNode response = this.apiHelper.getRequest(url, headers, queryParams);
        final JsonNode errors = response.get("error");
        if (errors != null) throw new RuntimeException(errors.toString());
        return StreamSupport.stream(response.spliterator(), false)
                .map(securityObj -> getEncryptionKeyWithService(
                        securityObj.get("kid").asText(),
                        customerUUID
                )).findFirst().orElse(null);
    }
}
