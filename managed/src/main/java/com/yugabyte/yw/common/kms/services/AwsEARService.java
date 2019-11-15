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

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil;
import com.yugabyte.yw.common.kms.util.AwsEARServiceUtil.KeyType;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;

enum AwsAlgorithm implements SupportedAlgorithmInterface {
    AES(Arrays.asList(128, 256));

    private List<Integer> keySizes;

    public List<Integer> getKeySizes() {
        return this.keySizes;
    }

    private AwsAlgorithm(List<Integer> keySizes) {
        this.keySizes = keySizes;
    }
}

public class AwsEARService extends EncryptionAtRestService<AwsAlgorithm> {
    /**
     * Constructor
     *
     * @param apiHelper is a library to make requests against a third party encryption key provider
     * @param keyProvider is a String representation of "SMARTKEY" (if it is valid in this context)
     */
    public AwsEARService() {
        super(KeyProvider.AWS);
    }

    /**
     * Search for and create/retrieve a CMK for an inputted universe
     *
     * @param customerUUID the customer associated to the universe
     * @param universeUUID the universe that the CMK is associated to (1:1 relationship)
     * @param policy the user-provided custom CMK policy
     * @return the CMK id associated with the universe
     */
    private String createOrRetrieveUniverseCMK(
            UUID customerUUID,
            UUID universeUUID,
            String policy
    ) {
        return AwsEARServiceUtil.createOrRetrieveCMKWithAlias(
                customerUUID,
                policy,
                "Yugabyte Universe Key",
                universeUUID.toString()
        );
    }

    @Override
    protected AwsAlgorithm[] getSupportedAlgorithms() { return AwsAlgorithm.values(); }

    @Override
    protected byte[] createKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    ) {
        byte[] result = null;
        String typeString = config.get("key_type");
        KeyType type = typeString == null || typeString.length() == 0 ?
                KeyType.DATA_KEY : KeyType.valueOf(typeString);
        String cmkId = createOrRetrieveUniverseCMK(
                customerUUID,
                universeUUID,
                config.get("cmk_policy")
        );
        switch (type) {
            case CMK:
                result = AwsEARServiceUtil.getCMKArn(customerUUID, cmkId).getBytes();
                break;
            default:
            case DATA_KEY:
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
                result = AwsEARServiceUtil.generateDataKey(customerUUID, cmkId, algorithm, keySize);
                if (result != null && result.length > 0) {
                    addKeyRef(customerUUID, universeUUID, result);
                }
                break;
        }
        return result;
    }

    @Override
    protected byte[] rotateKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        final String aliasName = AwsEARServiceUtil.generateAliasName(universeUUID.toString());
        final String cmkId = createOrRetrieveUniverseCMK(
                customerUUID,
                universeUUID,
                config.get("cmk_policy")
        );
        final String algorithm = config.get("algorithm");
        final int keySize = Integer.parseInt(config.get("key_size"));
        return AwsEARServiceUtil.generateDataKey(customerUUID, cmkId, algorithm, keySize);
    }

    @Override
    public byte[] retrieveKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            byte[] keyRef,
            Map<String, String> config
    ) {
        byte[] keyVal = null;
        try {
            String typeString = config.get("key_type");
            KeyType type = typeString == null || typeString.length() == 0 ?
                    KeyType.DATA_KEY : KeyType.valueOf(typeString);
            switch (type) {
                case CMK:
                    keyVal = keyRef;
                    break;
                default:
                case DATA_KEY:
                    keyVal = AwsEARServiceUtil.decryptUniverseKey(customerUUID, keyRef);
                    if (keyVal == null) LOG.warn("Could not retrieve key from key ref");
                    else EncryptionAtRestUtil
                            .setUniverseKeyCacheEntry(universeUUID, keyRef, keyVal);
                    break;
            }
        } catch (Exception e) {
            final String errMsg = "Error occurred retrieving encryption key";
            LOG.error(errMsg, e);
            throw new RuntimeException(errMsg, e);
        }
        return keyVal;
    }
}
