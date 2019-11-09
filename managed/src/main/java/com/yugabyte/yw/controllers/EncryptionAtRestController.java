/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

import static com.yugabyte.yw.models.helpers.CommonUtils.maskConfig;

public class EncryptionAtRestController extends AuthenticatedController {
    public static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestController.class);

    @Inject
    EncryptionAtRestManager keyManager;

    public Result createKMSConfig(UUID customerUUID, String keyProvider) {
        LOG.info(String.format(
                "Creating KMS configuration for customer %s with %s",
                customerUUID.toString(),
                keyProvider
        ));
        ObjectNode formData = (ObjectNode) request().body().asJson();
        EncryptionAtRestService keyService = keyManager.getServiceInstance(keyProvider);
        KmsConfig kmsConfig = keyService.createAuthConfig(customerUUID, formData);
        if (kmsConfig == null) {
            return ApiResponse.error(BAD_REQUEST, String.format(
                    "KMS customer configuration already exists for customer %s",
                    customerUUID.toString()
            ));
        }
        return ApiResponse.success(kmsConfig);
    }

    public Result getKMSConfig(UUID customerUUID, String keyProvider) {
        LOG.info(String.format(
                "Retrieving KMS configuration for customer %s with %s",
                customerUUID.toString(),
                keyProvider
        ));
        EncryptionAtRestService keyService = keyManager.getServiceInstance(keyProvider);
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
        LOG.info(String.format(
                "Listing KMS configurations for customer %s",
                customerUUID.toString()
        ));
        List<ObjectNode> kmsConfigs = Arrays.stream(EncryptionAtRestService.getKeyProviders())
                .filter(provider -> provider.getProviderService() != null)
                .map(provider -> {
                    EncryptionAtRestService keyService = keyManager
                            .getServiceInstance(provider.name());
                    ObjectNode obj = keyService.getAuthConfig(customerUUID);
                    if (obj != null) {
                        obj = (ObjectNode) maskConfig(obj);
                        obj.put("provider", provider.name());
                        obj.put("in_use", keyService.configInUse(customerUUID));
                    }
                    return obj;
                })
                .filter(authConfig -> authConfig != null)
                .collect(Collectors.toList());
        return ApiResponse.success(kmsConfigs);
    }

    public Result deleteKMSConfig(UUID customerUUID, String keyProvider) {
        LOG.info(String.format(
                "Deleting KMS configuration for customer %s with %s",
                customerUUID.toString(),
                keyProvider
        ));
        try {
            EncryptionAtRestService keyService = keyManager.getServiceInstance(keyProvider);
            keyService.deleteKMSConfig(customerUUID);
            return ApiResponse.success(String.format(
                    "KMS configuration for customer %s with key provider %s has been deleted",
                    customerUUID.toString(),
                    keyProvider
            ));
        } catch (Exception e) {
            final String errMsg = "Error caught attempting to delete KMS configuration";
            LOG.error(errMsg, e);
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result retrieveKey(UUID customerUUID, UUID universeUUID, String keyProvider) {
        EncryptionAtRestService keyService = null;
        LOG.info(String.format(
                "Retrieving universe key for customer %s with universe %s through %s",
                customerUUID.toString(),
                universeUUID.toString(),
                keyProvider
        ));
        byte[] recoveredKey = null;
        try {
            keyService = keyManager.getServiceInstance(keyProvider);
            recoveredKey = keyService.retrieveKey(customerUUID, universeUUID);
            if (recoveredKey == null || recoveredKey.length == 0) {
                final String errMsg = String.format(
                        "No key found for customer %s for universe %s with %s",
                        customerUUID.toString(),
                        universeUUID.toString(),
                        keyProvider
                );
                throw new RuntimeException(errMsg);
            }
            ObjectNode result = Json.newObject()
                    .put("value", Base64.getEncoder().encodeToString(recoveredKey));
            return ApiResponse.success(result);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not recover universe key from provider %s for customer %s " +
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
        LOG.info(String.format(
                "Creating universe key for customer %s with universe %s through %s",
                customerUUID.toString(),
                universeUUID.toString(),
                keyProvider
        ));
        try {
            keyService = keyManager.getServiceInstance(keyProvider);
            ObjectNode body = (ObjectNode) request().body().asJson();
            config = mapper.treeToValue(body, Map.class);
            byte[] encryptionKey = keyService.createKey(
                    universeUUID,
                    customerUUID,
                    config
            );
            if (encryptionKey == null || encryptionKey.length == 0) {
                final String errMsg = "KMS service returned an empty created key";
                throw new RuntimeException(errMsg);
            }
            ObjectNode result = Json.newObject()
                    .put("value", Base64.getEncoder().encodeToString(encryptionKey));
            return ApiResponse.success(result);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not create universe key with %s for customer %s " +
                            "with universe %s",
                    keyProvider,
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result rotateKey(UUID customerUUID, UUID universeUUID, String keyProvider) {
        ObjectMapper mapper = new ObjectMapper();
        EncryptionAtRestService keyService = null;
        Map<String, String> config = null;
        LOG.info(String.format(
                "Rotating universe key for customer %s with universe %s through %s",
                customerUUID.toString(),
                universeUUID.toString(),
                keyProvider
        ));
        try {
            keyService = keyManager.getServiceInstance(keyProvider);
            ObjectNode body = (ObjectNode) request().body().asJson();
            config = mapper.treeToValue(body, Map.class);
            byte[] rotatedKey = keyService.rotateKey(customerUUID, universeUUID, config);
            if (rotatedKey == null || rotatedKey.length == 0) {
                final String errMsg = "KMS service returned an empty rotated key";
                throw new RuntimeException(errMsg);
            }
            ObjectNode result = Json.newObject()
                    .put("value", Base64.getEncoder().encodeToString(rotatedKey));
            return ApiResponse.success(result);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not rotate universe key with %s for customer %s " +
                            "and universe %s",
                    keyProvider,
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result getKeyRefHistory(UUID customerUUID, UUID universeUUID, String keyProvider) {
        EncryptionAtRestService keyService = null;
        LOG.info(String.format(
                "Retrieving key ref history for customer %s and universe through %s",
                customerUUID.toString(),
                keyProvider
        ));
        try {
            keyService = keyManager.getServiceInstance(keyProvider);
            List<KmsHistory> keyRefHistory = keyService.getKeyRotationHistory(customerUUID, universeUUID);
            ObjectMapper mapper = new ObjectMapper();
            return ApiResponse.success(mapper.valueToTree(keyRefHistory));
        } catch (Exception e) {
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result removeKeyRefHistory(UUID customerUUID, UUID universeUUID, String keyProvider) {
        EncryptionAtRestService keyService = null;
        LOG.info(String.format(
                "Removing key ref for customer %s with universe %s through %s",
                customerUUID.toString(),
                universeUUID.toString(),
                keyProvider
        ));
        try {
            keyService = keyManager.getServiceInstance(keyProvider);
            keyService.removeKeyRotationHistory(customerUUID, universeUUID);
            return ApiResponse.success("Key ref was successfully removed");
        } catch (Exception e) {
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result getKeyRef(UUID customerUUID, UUID universeUUID, String keyProvider) {
        EncryptionAtRestService keyService = null;
        LOG.info(String.format(
                "Retrieving key ref for customer %s with universe %s through %s",
                customerUUID.toString(),
                universeUUID.toString(),
                keyProvider
        ));
        try {
            keyService = keyManager.getServiceInstance(keyProvider);
            if (keyService == null) {
                return ApiResponse.error(BAD_REQUEST, String.format(
                        "Could not retrieve key service for %s",
                        keyProvider
                ));
            }
            byte[] keyRef = keyService.getKeyRef(customerUUID, universeUUID);
            if (keyRef == null || keyRef.length == 0) {
                return ApiResponse.error(BAD_REQUEST, String.format(
                        "Could not retrieve key service for customer %s with %s",
                        customerUUID.toString(),
                        keyProvider
                ));
            }
            return ApiResponse.success(Json.newObject().put(
                    "key_ref", Base64.getEncoder().encodeToString(keyRef)
            ));
        } catch (Exception e) {
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }

    public Result addKeyRef(UUID customerUUID, UUID universeUUID, String keyProvider) {
        EncryptionAtRestService keyService = null;
        LOG.info(String.format(
                "Adding key ref for customer %s with universe %s through %s",
                customerUUID.toString(),
                universeUUID.toString(),
                keyProvider
        ));
        try {
            ObjectNode formData = (ObjectNode) request().body().asJson();
            byte[] ref = Base64.getDecoder().decode(formData.get("key_ref").asText());
            keyService = keyManager.getServiceInstance(keyProvider);
            keyService.addKeyRef(customerUUID, universeUUID, ref);
            return ApiResponse.success("Key ref was successfully added");
        } catch (Exception e) {
            return ApiResponse.error(BAD_REQUEST, e.getMessage());
        }
    }
}
