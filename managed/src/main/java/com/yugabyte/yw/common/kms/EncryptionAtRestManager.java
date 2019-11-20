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

import com.google.inject.Inject;
import com.yugabyte.yw.common.kms.services.EncryptionAtRestService;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.UniverseTaskParams.EncryptionAtRestConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.KmsHistoryId;
import com.yugabyte.yw.models.Universe;
import java.lang.reflect.Constructor;
import java.util.Base64;
import java.util.UUID;
import javax.inject.Singleton;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
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
            UUID configUUID,
            UUID universeUUID,
            EncryptionAtRestConfig config
    ) {
        EncryptionAtRestService keyService;
        KmsConfig kmsConfig;
        byte[] universeKeyData = null;
        Universe universe = Universe.get(universeUUID);
        if (universe == null) {
            String errMsg = String.format("Invalid Universe UUID: %s", universeUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        try {
            kmsConfig = KmsConfig.get(configUUID);
            keyService = getServiceInstance(kmsConfig.keyProvider.name());
            if (getNumKeyRotations(universeUUID, configUUID) == 0) {
                LOG.info(String.format(
                        "Creating universe key for universe %s",
                        universeUUID.toString()
                ));
                universeKeyData = keyService.createKey(
                        universeUUID,
                        configUUID,
                        config
                );
            } else {
                LOG.info(String.format(
                        "Rotating universe key for universe %s",
                        universeUUID.toString()
                ));
                universeKeyData = keyService.rotateKey(
                        universeUUID,
                        configUUID,
                        config
                );
            }
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to generate universe key for universe %s",
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
        }
        return universeKeyData;
    }

    public UUID getCurrentKMSConfigUUID(UUID universeUUID) {
        KmsHistory latestHistory = KmsHistory.getCurrentConfig(
                universeUUID,
                KmsHistoryId.TargetType.UNIVERSE_KEY
        );
        return latestHistory.configUuid;
    }

    public UUID getKeyRefKMSConfigUUID(UUID universeUUID, byte[] keyRef) {
        KmsHistory keyRefEntry = KmsHistory.getKeyRefConfig(
                universeUUID,
                Base64.getEncoder().encodeToString(keyRef),
                KmsHistoryId.TargetType.UNIVERSE_KEY
        );
        return keyRefEntry.configUuid;
    }

    public byte[] getCurrentUniverseKeyRef(UUID universeUUID) {
        return getCurrentUniverseKeyRef(
                universeUUID,
                Universe.get(universeUUID).getKMSConfigUUID()
        );
    }

    public byte[] getCurrentUniverseKeyRef(UUID universeUUID, UUID configUUID) {
        byte[] keyRef = null;
        EncryptionAtRestService keyService;
        try {
            keyService = getServiceInstance(KmsConfig.get(configUUID).keyProvider.name());
            keyRef = keyService.getKeyRef(configUUID, universeUUID);
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to retrieve the current universe key ref for " +
                            "universe %s with config %s",
                    universeUUID.toString(),
                    configUUID.toString()
            );
            LOG.error(errMsg, e);
        }
        return keyRef;
    }

    public byte[] getCurrentUniverseKey(UUID universeUUID, byte[] keyRef) {
        return getCurrentUniverseKey(
                universeUUID,
                Universe.get(universeUUID).getKMSConfigUUID(),
                keyRef
        );
    }

    public byte[] getCurrentUniverseKey(UUID universeUUID, UUID configUUID, byte[] keyRef) {
        byte[] keyVal = null;
        EncryptionAtRestService keyService;
        Universe universe = Universe.get(universeUUID);
        try {
            keyService = getServiceInstance(KmsConfig.get(configUUID).keyProvider.name());
            keyVal = keyService.retrieveKey(
                    universeUUID,
                    configUUID,
                    keyRef,
                    universe.getEncryptionAtRestConfig()
            );
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to retrieve the current universe key for universe %s " +
                            "with config %s",
                    universeUUID.toString(),
                    configUUID.toString()
            );
            LOG.error(errMsg, e);
        }
        return keyVal;
    }

    public int getNumKeyRotations(UUID universeUUID) {
        return getNumKeyRotations(universeUUID, null);
    }

    public int getNumKeyRotations(UUID universeUUID, UUID configUUID) {
        int numRotations = 0;
        Universe universe = Universe.get(universeUUID);
        if (universe == null) {
            String errMsg = String.format("Invalid Universe UUID: %s", universeUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        try {
            List<KmsHistory> keyRotations = configUUID == null ?
                    KmsHistory.getAllTargetKeyRefs(
                            universeUUID,
                            KmsHistoryId.TargetType.UNIVERSE_KEY
                    ) :
                    KmsHistory.getAllConfigTargetKeyRefs(
                            configUUID,
                            universeUUID,
                            KmsHistoryId.TargetType.UNIVERSE_KEY
                    );
            if (keyRotations != null) numRotations = keyRotations.size();
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to retrieve the number of key rotations " +
                            "universe %s",
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
        }
        return numRotations;
    }

    public void clearUniverseKeyHistory(UUID universeUUID) {
        Universe universe = Universe.get(universeUUID);
        if (universe == null) {
            String errMsg = String.format("Invalid Universe UUID: %s", universeUUID.toString());
            LOG.error(errMsg);
            throw new IllegalArgumentException(errMsg);
        }
        try {
            KmsHistory.deleteAllTargetKeyRefs(universeUUID, KmsHistoryId.TargetType.UNIVERSE_KEY);
        } catch (Exception e) {
            String errMsg = String.format(
                    "Error attempting to clear the universe key history for universe %s",
                    universeUUID.toString()
            );
            LOG.error(errMsg, e);
            throw e;
        }
    }

    public void cleanupEncryptionAtRest(UUID customerUUID, UUID universeUUID) {
        clearUniverseKeyHistory(universeUUID);
        KmsConfig.listKMSConfigs(customerUUID).stream().forEach(config -> {
            EncryptionAtRestService keyService = getServiceInstance(config.keyProvider.name());
            keyService.cleanup(universeUUID, config.configUUID);
        });
    }
}
