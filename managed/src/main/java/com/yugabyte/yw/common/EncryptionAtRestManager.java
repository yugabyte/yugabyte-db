/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.avaje.ebean.annotation.EnumValue;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import java.lang.reflect.Constructor;
import java.util.UUID;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.security.crypto.encrypt.Encryptors;
import org.springframework.security.crypto.encrypt.TextEncryptor;
import play.api.Play;
import play.libs.Json;
import java.time.Clock;
import java.time.format.DateTimeFormatter;
import java.time.LocalDateTime;
import java.util.List;
import java.util.Map;


@Singleton
public class EncryptionAtRestManager {
    public static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestManager.class);

    @Inject
    ApiHelper apiHelper;

    /**
     * A list of third party encryption key providers that YB currently supports and the
     * corresponding service impl and any already instantiated classes
     * (such that each impl is a singleton)
     */
    public enum KeyProvider {
        @EnumValue("AWS")
        AWS(AwsEARService.class),

        @EnumValue("SMARTKEY")
        SMARTKEY(SmartKeyEARService.class);

        private Class providerService;

        private EncryptionAtRestService instance;

        public Class getProviderService() {
            return this.providerService;
        }

        public EncryptionAtRestService getServiceInstance() { return this.instance; }

        public void setServiceInstance(EncryptionAtRestService instance) {
            this.instance = instance;
        }

        private KeyProvider(Class providerService) {
            this.providerService = providerService;
            this.instance = null;
        }
    }

    @Inject
    public EncryptionAtRestManager() {}

    public String generateSalt(UUID customerUUID, KeyProvider keyProvider) {
        final String saltBase = "%s%s";
        final String salt = String.format(
                saltBase,
                customerUUID.toString().replace("-", ""),
                keyProvider.name().hashCode()
        );
        return salt.length() % 2 == 0 ? salt : salt + "0";
    }

    public <N extends JsonNode> ObjectNode maskConfigData(
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

    public JsonNode unmaskConfigData(
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

    private Constructor getConstructor(Class serviceClass) {
        Constructor serviceConstructor = null;
        Class[] parameterTypes;
        for (Constructor constructor : serviceClass.getConstructors()) {
            parameterTypes = constructor.getParameterTypes();
            if (
                    constructor.getParameterCount() == 3 &&
                            ApiHelper.class.isAssignableFrom(parameterTypes[0]) &&
                            KeyProvider.class.isAssignableFrom(parameterTypes[1]) &&
                            EncryptionAtRestManager.class.isAssignableFrom(parameterTypes[2])
            ) {
                serviceConstructor = constructor;
                break;
            }
        }
        return serviceConstructor;
    }

    public EncryptionAtRestService getServiceInstance(
            String keyProvider
    ) { return getServiceInstance(keyProvider, false); }

    public EncryptionAtRestService getServiceInstance(
            String keyProvider,
            boolean forceNewInstance
    ) {
        KeyProvider serviceProvider = null;
        EncryptionAtRestService serviceInstance = null;
        try {
            for (KeyProvider providerImpl : KeyProvider.values()) {
                if (providerImpl.name().equals(keyProvider)) {
                    serviceProvider = providerImpl;
                    break;
                }
            }
            if (serviceProvider == null) {
                final String errMsg = String.format(
                        "Encryption service provider %s is not supported",
                        keyProvider
                );
                LOG.error(errMsg);
                throw new IllegalArgumentException(errMsg);
            }
            serviceInstance = serviceProvider.getServiceInstance();
            if (forceNewInstance || serviceInstance == null) {
                final Class serviceClass = serviceProvider.getProviderService();
                if (serviceClass == null) {
                    final String errMsg = String.format(
                            "Encryption service provider %s has not been implemented yet",
                            keyProvider
                    );
                    LOG.error(errMsg);
                    throw new IllegalArgumentException(errMsg);
                }
                final Constructor serviceConstructor = getConstructor(serviceClass);
                if (serviceConstructor == null) {
                    final String errMsg = String.format(
                            "No suitable public constructor found for service provider %s",
                            keyProvider
                    );
                    LOG.error(errMsg);
                    throw new InstantiationException(errMsg);
                }
                serviceInstance = (EncryptionAtRestService) serviceConstructor.newInstance(
                        this.apiHelper,
                        serviceProvider,
                        Play.current().injector().instanceOf(EncryptionAtRestManager.class)
                );
                if (serviceInstance != null) serviceProvider.setServiceInstance(serviceInstance);
            }
        } catch (Exception e) {
            final String errMsg = String.format(
                    "Error occurred attempting to retrieve encryption key service for " +
                            "key provider %s",
                    keyProvider
            );
            LOG.error(errMsg, e);
            serviceInstance = null;
        }
        return serviceInstance;
    }

    public byte[] generateUniverseKey(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        return generateUniverseKey(customerUUID, universeUUID, config, false);
    }

    public byte[] generateUniverseKey(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config,
            boolean forceCreate
    ) {
        EncryptionAtRestService keyService;
        String kmsProvider;
        byte[] universeKeyData = null;
        Customer customer = Customer.get(customerUUID);
        if (customer == null) {
            String errMsg = String.format("Invalid Customer UUID: %s", customerUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        Universe universe = Universe.get(universeUUID);
        if (universe == null) {
            String errMsg = String.format("Invalid Universe UUID: %s", universeUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        try {
            kmsProvider = config.get("kms_provider");
            keyService = getServiceInstance(kmsProvider);
            universe = Universe.get(universeUUID);
            boolean universeHasKeyRotationHistory = getNumKeyRotations(
                    customerUUID,
                    universeUUID,
                    config
            ) > 0;
            if (forceCreate || (!universe.isEncryptedAtRest() && !universeHasKeyRotationHistory)) {
                LOG.info(String.format(
                        "Creating universe key for universe %s",
                        universeUUID.toString()
                ));
                universeKeyData = keyService.createKey(universeUUID, customerUUID, config);
            } else {
                LOG.info(String.format(
                        "Rotating universe key for universe %s",
                        universeUUID.toString()
                ));
                universeKeyData = keyService.rotateKey(customerUUID, universeUUID, config);
            }
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to generate universe key for customer %s and universe %s",
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
        }
        return universeKeyData;
    }

    public int getNumKeyRotations(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        int numRotations = 0;
        EncryptionAtRestService keyService;
        Customer customer = Customer.get(customerUUID);
        if (customer == null) {
            String errMsg = String.format("Invalid Customer UUID: %s", customerUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        Universe universe = Universe.get(universeUUID);
        if (universe == null) {
            String errMsg = String.format("Invalid Universe UUID: %s", universeUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        try {
            String keyProvider = config.get("kms_provider");
            keyService = getServiceInstance(keyProvider);
            List<KmsHistory> keyRotations = keyService
                    .getKeyRotationHistory(customerUUID, universeUUID);
            if (keyRotations != null) numRotations = keyRotations.size();
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to retrieve the number of key rotations " +
                            "for customer %s and universe %s",
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
        }
        return numRotations;
    }

    public void clearUniverseKeyHistory(UUID customerUUID, UUID universeUUID) {
        Customer customer = Customer.get(customerUUID);
        if (customer == null) {
            String errMsg = String.format("Invalid Customer UUID: %s", customerUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        Universe universe = Universe.get(universeUUID);
        if (universe == null) {
            String errMsg = String.format("Invalid Universe UUID: %s", universeUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        clearUniverseKeyHistory(
                customerUUID,
                universeUUID,
                universe.getEncryptionAtRestConfig()
        );
    }

    public void clearUniverseKeyHistory(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        EncryptionAtRestService keyService;
        Customer customer = Customer.get(customerUUID);
        if (customer == null) {
            String errMsg = String.format("Invalid Customer UUID: %s", customerUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        Universe universe = Universe.get(universeUUID);
        if (universe == null) {
            String errMsg = String.format("Invalid Universe UUID: %s", universeUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        try {
            String keyProvider = config.get("kms_provider");
            keyService = getServiceInstance(keyProvider);
            keyService.removeKeyRotationHistory(customerUUID, universeUUID);
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to clear the universe key history for universe %s",
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
            throw e;
        }
    }
}
