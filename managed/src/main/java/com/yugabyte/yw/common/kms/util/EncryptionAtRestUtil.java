/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.models.KmsConfig;
import java.lang.reflect.Constructor;
import java.util.Base64;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.security.crypto.encrypt.Encryptors;
import org.springframework.security.crypto.encrypt.TextEncryptor;
import play.api.Play;
import play.libs.Json;

public class EncryptionAtRestUtil {
    protected static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestUtil.class);

    public static Constructor getConstructor(Class serviceClass) {
        Constructor serviceConstructor = null;
        Class[] parameterTypes;
        for (Constructor constructor : serviceClass.getConstructors()) {
            parameterTypes = constructor.getParameterTypes();
            if (constructor.getParameterCount() == 0) {
                serviceConstructor = constructor;
                break;
            }
        }
        return serviceConstructor;
    }

    public static ObjectNode getAuthConfig(UUID customerUUID, KeyProvider keyProvider) {
        final ObjectNode config = KmsConfig.getKMSAuthObj(customerUUID, keyProvider);
        return (ObjectNode) EncryptionAtRestUtil
                .unmaskConfigData(customerUUID, config, keyProvider);
    }

    public static <N extends JsonNode> ObjectNode maskConfigData(
            UUID customerUUID,
            N config,
            KeyProvider keyProvider
    ) {
        try {
            final ObjectMapper mapper = new ObjectMapper();
            final String salt = generateSalt(customerUUID, keyProvider);
            final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
            final String encryptedConfig = encryptor.encrypt(mapper.writeValueAsString(config));
            return Json.newObject().put("encrypted", encryptedConfig);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not encrypt %s KMS configuration for customer %s",
                    keyProvider.name(),
                    customerUUID.toString()
            );
            LOG.error(errMsg, e);
            return null;
        }
    }

    public static JsonNode unmaskConfigData(
            UUID customerUUID,
            ObjectNode config,
            KeyProvider keyProvider
    ) {
        if (config == null) return null;
        try {
            final ObjectMapper mapper = new ObjectMapper();
            final String encryptedConfig = config.get("encrypted").asText();
            final String salt = generateSalt(customerUUID, keyProvider);
            final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
            final String decryptedConfig = encryptor.decrypt(encryptedConfig);
            return mapper.readValue(decryptedConfig, JsonNode.class);
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Could not decrypt %s KMS configuration for customer %s",
                    keyProvider.name(),
                    customerUUID.toString()
            );
            LOG.error(errMsg, e);
            return null;
        }
    }

    public static String generateSalt(UUID customerUUID, KeyProvider keyProvider) {
        final String saltBase = "%s%s";
        final String salt = String.format(
                saltBase,
                customerUUID.toString().replace("-", ""),
                keyProvider.name().hashCode()
        );
        return salt.length() % 2 == 0 ? salt : salt + "0";
    }

    public static byte[] getUniverseKeyCacheEntry(UUID universeUUID, byte[] keyRef) {
        LOG.info(String.format(
                "Retrieving universe key cache entry for universe %s and keyRef %s",
                universeUUID.toString(),
                Base64.getEncoder().encodeToString(keyRef)
        ));
        return Play.current().injector().instanceOf(EncryptionAtRestUniverseKeyCache.class)
                .getCacheEntry(universeUUID, keyRef);
    }

    public static void setUniverseKeyCacheEntry(UUID universeUUID, byte[] keyRef, byte[] keyVal) {
        LOG.info(String.format(
                "Setting universe key cache entry for universe %s and keyRef %s",
                universeUUID.toString(),
                Base64.getEncoder().encodeToString(keyRef)
        ));
        Play.current().injector().instanceOf(EncryptionAtRestUniverseKeyCache.class)
                .setCacheEntry(universeUUID, keyRef, keyVal);
    }

    public static void removeUniverseKeyCacheEntry(UUID universeUUID) {
        LOG.info(String.format(
                "Removing universe key cache entry for universe %s",
                universeUUID.toString()
        ));
        Play.current().injector().instanceOf(EncryptionAtRestUniverseKeyCache.class)
                .removeCacheEntry(universeUUID);
    }
}
