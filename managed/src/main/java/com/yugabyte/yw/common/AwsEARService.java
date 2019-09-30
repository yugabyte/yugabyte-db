// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.amazonaws.auth.AWSStaticCredentialsProvider;
import com.amazonaws.auth.BasicAWSCredentials;
import com.amazonaws.regions.Regions;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagement;
import com.amazonaws.services.identitymanagement.AmazonIdentityManagementClientBuilder;
import com.amazonaws.services.identitymanagement.model.AccessKeyMetadata;
import com.amazonaws.services.identitymanagement.model.ListAccessKeysRequest;
import com.amazonaws.services.identitymanagement.model.ListAccessKeysResult;
import com.amazonaws.services.identitymanagement.model.ListUsersRequest;
import com.amazonaws.services.identitymanagement.model.ListUsersResult;
import com.amazonaws.services.identitymanagement.model.User;
import com.amazonaws.services.kms.AWSKMS;
import com.amazonaws.services.kms.AWSKMSClientBuilder;
import com.amazonaws.services.kms.model.AliasListEntry;
import com.amazonaws.services.kms.model.CreateAliasRequest;
import com.amazonaws.services.kms.model.CreateAliasResult;
import com.amazonaws.services.kms.model.CreateKeyRequest;
import com.amazonaws.services.kms.model.CreateKeyResult;
import com.amazonaws.services.kms.model.DecryptRequest;
import com.amazonaws.services.kms.model.DecryptResult;
import com.amazonaws.services.kms.model.GenerateDataKeyWithoutPlaintextRequest;
import com.amazonaws.services.kms.model.GenerateDataKeyWithoutPlaintextResult;
import com.amazonaws.services.kms.model.ListAliasesRequest;
import com.amazonaws.services.kms.model.ListAliasesResult;
import com.amazonaws.services.kms.model.UpdateAliasRequest;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.BinaryNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;

class AwsKMSCredentials {
    private String accessKey;
    private String secretKey;
    private Regions region;

    public String getAccessKey() { return this.accessKey; }
    public String getSecretKey() { return this.secretKey; }
    public Regions getRegion() { return this.region; }

    public AwsKMSCredentials(ObjectNode authConfig) {
        this.accessKey = authConfig.get("access_key_id").asText();
        this.secretKey = authConfig.get("secret_key_id").asText();
        this.region = Enum.valueOf(Regions.class, authConfig.get("region").asText());
    }
}

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

// TODO: (Daniel)
// 1) Provide multiple options for inputting credentials:
//      a) pass in access_key/secret_key directly
//      b) use existing AWS cloud provider
public class AwsEARService extends EncryptionAtRestService<AwsAlgorithm> {
    /**
     * Constructor
     *
     * @param apiHelper is a library to make requests against a third party encryption key provider
     * @param keyProvider is a String representation of "SMARTKEY" (if it is valid in this context)
     */
    public AwsEARService(ApiHelper apiHelper, String keyProvider) {
        super(apiHelper, keyProvider);
    }

    /**
     *
     * @param creds
     * @return
     */
    private AWSKMS getClient(AwsKMSCredentials creds) {
        final BasicAWSCredentials awsCreds = new BasicAWSCredentials(
                creds.getAccessKey(),
                creds.getSecretKey()
        );
        return AWSKMSClientBuilder
                .standard()
                .withCredentials(new AWSStaticCredentialsProvider(awsCreds))
                .withRegion(creds.getRegion())
                .build();
    }

    /**
     *
     * @param universeUUID
     * @param customerUUID
     * @param kId
     */
    private void createAlias(UUID universeUUID, UUID customerUUID, String kId) {
        final String aliasNameBase = "alias/%s";
        final CreateAliasRequest req = new CreateAliasRequest()
                .withAliasName(String.format(aliasNameBase, universeUUID.toString()))
                .withTargetKeyId(kId);
        getClient(new AwsKMSCredentials(getAuthConfig(customerUUID))).createAlias(req);
    }

    /**
     *
     * @param customerUUID
     * @param aliasName
     * @return
     */
    private AliasListEntry getAlias(UUID customerUUID, String aliasName) {
        final String aliasNameBase = "alias/%s";
        ListAliasesRequest req = new ListAliasesRequest().withLimit(100);
        AliasListEntry uniAlias = null;
        boolean done = false;
        while (!done) {
            ListAliasesResult result = getClient(
                    new AwsKMSCredentials(getAuthConfig(customerUUID))
            ).listAliases(req);
            for (AliasListEntry alias : result.getAliases()) {
                if (alias.getAliasName().equals(String.format(aliasNameBase, aliasName))) {
                    uniAlias = alias;
                    break;
                }
            }
            req.setMarker(result.getNextMarker());
            if (!result.getTruncated()) {
                done = true;
            }
        }

        return uniAlias;
    }

    /**
     *
     * @param customerUUID
     * @param universeUUID
     * @param newTargetKeyId
     */
    private void updateAliasTarget(UUID customerUUID, UUID universeUUID, String newTargetKeyId) {
        UpdateAliasRequest req = new UpdateAliasRequest()
                .withAliasName(universeUUID.toString())
                .withTargetKeyId(newTargetKeyId);
        getClient(new AwsKMSCredentials(getAuthConfig(customerUUID))).updateAlias(req);
    }

    private byte[] retrieveEncryptedUniverseKeyLocal(
            UUID customerUUID,
            UUID universeUUID
    ) throws Exception {
        final ObjectNode authConfig = getAuthConfig(customerUUID);
        final JsonNode serializedKeyBytes = authConfig.get(getUniverseMasterKeyName(universeUUID));
        if (serializedKeyBytes == null) {
            final String errMsg = String.format(
                    "No universe master key exists locally for customer %s and universe %s",
                    customerUUID.toString(),
                    universeUUID.toString()
            );
            LOG.warn(errMsg);
        }
        return serializedKeyBytes == null ? null : serializedKeyBytes.binaryValue();
    }

    /**
     *
     * @param customerUUID
     * @param encryptedUniverseKey
     * @return
     */
    private byte[] decryptUniverseKey(UUID customerUUID, byte[] encryptedUniverseKey) {
        if (encryptedUniverseKey == null) return null;
        ByteBuffer encryptedKeyBuffer = ByteBuffer.wrap(encryptedUniverseKey);
        encryptedKeyBuffer.rewind();
        final DecryptRequest req = new DecryptRequest().withCiphertextBlob(encryptedKeyBuffer);
        ByteBuffer decryptedKeyBuffer = getClient(
                new AwsKMSCredentials(getAuthConfig(customerUUID))
        ).decrypt(req).getPlaintext();
        decryptedKeyBuffer.rewind();
        byte[] decryptedUniverseKey = new byte[decryptedKeyBuffer.remaining()];
        decryptedKeyBuffer.get(decryptedUniverseKey);
        return decryptedUniverseKey;
    }

    /**
     *
     * @param customerUUID
     * @param cmkId
     * @param algorithm
     * @param keySize
     * @return
     */
    private byte[] generateDataKey(UUID customerUUID, String cmkId, String algorithm, int keySize) {
        final String keySpecBase = "%s_%s";
        final GenerateDataKeyWithoutPlaintextRequest dataKeyRequest =
                new GenerateDataKeyWithoutPlaintextRequest()
                        .withKeyId(cmkId)
                        .withKeySpec(
                                String.format(keySpecBase, algorithm, Integer.toString(keySize))
                        );
        ByteBuffer encryptedKeyBuffer =
                getClient(new AwsKMSCredentials(getAuthConfig(customerUUID)))
                        .generateDataKeyWithoutPlaintext(dataKeyRequest)
                        .getCiphertextBlob();
        encryptedKeyBuffer.rewind();
        byte[] encryptedKeyBytes = new byte[encryptedKeyBuffer.remaining()];
        encryptedKeyBuffer.get(encryptedKeyBytes);
        return encryptedKeyBytes;
    }

    @Override
    protected AwsAlgorithm[] getSupportedAlgorithms() { return AwsAlgorithm.values(); }

    @Override
    protected String createEncryptionKeyWithService(
            UUID universeUUID,
            UUID customerUUID,
            Map<String, String> config
    ) {
        final CreateKeyRequest req = new CreateKeyRequest()
                .withDescription("Yugaware KMS Integration")
                .withPolicy(config.get("cmk_policy"));
        final CreateKeyResult result = getClient(
                new AwsKMSCredentials(getAuthConfig(customerUUID))
        ).createKey(req);
        final String kId = result.getKeyMetadata().getKeyId();
        if (getAlias(customerUUID, universeUUID.toString()) == null) {
            createAlias(universeUUID, customerUUID, kId);
        } else {
            updateAliasTarget(customerUUID, universeUUID, kId);
        }
        return kId;
    }

    private String getUniverseMasterKeyName(UUID universeUUID) {
        return String.format("data_key_%s", universeUUID.toString());
    }

    @Override
    protected byte[] getEncryptionKeyWithService(
            String kId,
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        final String algorithm = config.get("algorithm");
        final int keySize = Integer.parseInt(config.get("key_size"));
        final byte[] encryptedKeyBytes = generateDataKey(customerUUID, kId, algorithm, keySize);
        final ObjectMapper mapper = new ObjectMapper();
        updateAuthConfig(customerUUID, ImmutableMap.of(
                getUniverseMasterKeyName(universeUUID), mapper.valueToTree(encryptedKeyBytes)
        ));
        return recoverEncryptionKeyWithService(customerUUID, null, config);
    }

    @Override
    public byte[] recoverEncryptionKeyWithService(
            UUID customerUUID,
            UUID universeUUID,
            Map<String, String> config
    ) {
        byte[] result = null;
        try {
            result = decryptUniverseKey(
                    customerUUID,
                    retrieveEncryptedUniverseKeyLocal(customerUUID, universeUUID)
            );
        } catch (Exception e) {
            final String errMsg = "Could not recover encryption key";
            LOG.error(errMsg, e);
        }
        return result;
    }
}
