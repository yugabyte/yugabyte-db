/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms;

import com.avaje.ebean.annotation.EnumValue;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.kms.services.AwsEARService;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.kms.services.SmartKeyEARService;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUniverseKeyCache;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import java.lang.reflect.Constructor;
import java.util.Base64;
import java.util.UUID;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;
import play.libs.Json;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Singleton
public class EncryptionAtRestManager {
    public static final Logger LOG = LoggerFactory.getLogger(EncryptionAtRestManager.class);

    @Inject
    public EncryptionAtRestManager() {}

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
                final Constructor serviceConstructor = EncryptionAtRestUtil
                        .getConstructor(serviceClass);
                if (serviceConstructor == null) {
                    final String errMsg = String.format(
                            "No suitable public constructor found for service provider %s",
                            keyProvider
                    );
                    LOG.error(errMsg);
                    throw new InstantiationException(errMsg);
                }
                serviceInstance = (EncryptionAtRestService) serviceConstructor.newInstance();
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
            if (forceCreate || (!universe.isEncryptedAtRest() &&
                    !(getNumKeyRotations(customerUUID, universeUUID, config) > 0))) {
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

    public byte[] getCurrentUniverseKey(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config,
            byte[] keyRef
    ) {
        byte[] keyVal = null;
        EncryptionAtRestService keyService;
        String kmsProvider;
        Universe universe = Universe.get(universeUUID);
        try {
            kmsProvider = config.get("kms_provider");
            keyService = getServiceInstance(kmsProvider);
            keyVal = keyService.retrieveKey(customerUUID, universeUUID, keyRef, config);
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to retrieve the current universe key for " +
                            "customer %s and universe %s",
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
        }
        return keyVal;
    }

    public byte[] getCurrentUniverseKeyRef(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        byte[] keyRef = null;
        EncryptionAtRestService keyService;
        String kmsProvider;
        Universe universe = Universe.get(universeUUID);
        try {
            kmsProvider = config.get("kms_provider");
            keyService = getServiceInstance(kmsProvider);
            keyRef = keyService.getKeyRef(customerUUID, universeUUID);
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to retrieve the current universe key ref for " +
                            "customer %s and universe %s",
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
        }
        return keyRef;
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
