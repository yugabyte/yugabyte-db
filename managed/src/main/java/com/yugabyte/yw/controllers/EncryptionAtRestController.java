// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.EncryptionAtRestService;
import com.yugabyte.yw.models.CustomerConfig;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

public class EncryptionAtRestController extends AuthenticatedController {
    public static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestController.class);

    @Inject
    ApiHelper apiHelper;

    public Result createKMSConfig(UUID customerUUID, String keyProvider) {
        ObjectNode formData = (ObjectNode) request().body().asJson();
        EncryptionAtRestService keyService = EncryptionAtRestService.getServiceInstance(
                apiHelper,
                keyProvider
        );
        CustomerConfig kmsConfig = keyService.createAuthConfig(customerUUID, formData);
        if (kmsConfig == null) {
            return ApiResponse.error(BAD_REQUEST, String.format(
                    "KMS customer configuration already exists for customer %s",
                    customerUUID.toString()
            ));
        }
        return ApiResponse.success(kmsConfig);
    }

    public Result getKMSConfig(UUID customerUUID, String keyProvider) {
        EncryptionAtRestService keyService = EncryptionAtRestService.getServiceInstance(
                apiHelper,
                keyProvider
        );
        ObjectNode kmsConfig = keyService.getAuthConfig(customerUUID);
        if (kmsConfig == null) {
            return ApiResponse.error(BAD_REQUEST, String.format(
                    "No KMS configuration found for customer %s with %s",
                    customerUUID.toString(),
                    keyProvider
            ));
        }
        return ApiResponse.success(kmsConfig);
    }

    public Result listKMSConfigs(UUID customerUUID) {
        List<ObjectNode> kmsConfigs = Arrays.stream(EncryptionAtRestService.KeyProvider.values())
                .filter(provider -> provider.getProviderService() != null)
                .map(provider -> {
                    EncryptionAtRestService keyService = EncryptionAtRestService
                            .getServiceInstance(
                                    apiHelper,
                                    provider.name()
                            );
                    return keyService.getAuthConfig(customerUUID);
                })
                .filter(authConfig -> authConfig != null)
                .collect(Collectors.toList());
        return ApiResponse.success(kmsConfigs);
    }

    public Result deleteKMSConfig(UUID customerUUID, String keyProvider) {
        EncryptionAtRestService keyService = EncryptionAtRestService.getServiceInstance(
                apiHelper,
                keyProvider
        );
        CustomerConfig kmsConfig = keyService.getKMSConfig(customerUUID);
        kmsConfig.delete();
        return ApiResponse.success(String.format(
                "KMS configuration for customer %s with key provider %s has been deleted",
                customerUUID.toString(),
                keyProvider
        ));
    }

    public Result recoverEncryptionKey(UUID customerUUID, UUID universeUUID, String keyProvider) {
        EncryptionAtRestService keyService = null;
        String recoveredKey = null;
        try {
            keyService = EncryptionAtRestService.getServiceInstance(
                    apiHelper,
                    keyProvider
            );
            recoveredKey = keyService.recoverEncryptionKeyWithService(customerUUID, universeUUID);
            if (recoveredKey == null || recoveredKey.isEmpty()) {
                final String errMsg = String.format(
                        "No key found for customer %s for universe %s with provider %s",
                        customerUUID.toString(),
                        universeUUID.toString(),
                        keyProvider
                );
                throw new RuntimeException(errMsg);
            }
            ObjectNode result = Json.newObject().put("value", recoveredKey);
            return ApiResponse.success(result);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not recover encryption key from provider %s for customer %s " +
                            "with universe %s",
                    keyProvider,
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }

    }

    public Result createKey(UUID customerUUID, UUID universeUUID, String keyProvider) {
        ObjectMapper mapper = new ObjectMapper();
        EncryptionAtRestService keyService = null;
        Map<String, String> config = null;
        try {
            keyService = EncryptionAtRestService.getServiceInstance(
                    apiHelper,
                    keyProvider
            );
            ObjectNode body = (ObjectNode) request().body().asJson();
            config = mapper.treeToValue(body, Map.class);
            String encryptionKey = keyService.createAndRetrieveEncryptionKey(
                    universeUUID,
                    customerUUID,
                    config
            );
            if (encryptionKey == null || encryptionKey.isEmpty()) {
                final String errMsg = "Error creating encryption key";
                throw new RuntimeException(errMsg);
            }
            ObjectNode result = Json.newObject().put("value", encryptionKey);
            return ApiResponse.success(result);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not create encryption key with provider %s for customer %s " +
                            "with universe %s",
                    keyProvider,
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }
}
