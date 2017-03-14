// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

import javax.inject.Singleton;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

@Singleton
public class AccessManager extends DevopsBase {
    public static final Logger LOG = LoggerFactory.getLogger(AccessManager.class);

    @Inject
    play.Configuration appConfig;

    private static final String YB_CLOUD_COMMAND_TYPE = "access";

    @Override
    protected String getCommandType() {
        return YB_CLOUD_COMMAND_TYPE;
    }

    public enum KeyType {
        PUBLIC,
        PRIVATE;

        public String getExtension() {
            switch(this) {
                case PUBLIC:
                    return ".pub";
                case PRIVATE:
                    return ".pem";
                default:
                    return null;
            }
        }
    }

    private String getKeyFilePath(UUID providerUUID) {
        File keyBasePathName = new File(appConfig.getString("yb.storage.path"), "/keys");
        if (!keyBasePathName.exists() && !keyBasePathName.mkdirs()) {
            throw new RuntimeException("Key path " +
                keyBasePathName.getAbsolutePath() + " doesn't exists.");
        }

        File keyFilePath = new File(keyBasePathName.getAbsoluteFile(), providerUUID.toString());
        if (keyFilePath.isDirectory() || keyFilePath.mkdirs()) {
            return keyFilePath.getAbsolutePath();
        }

        throw new RuntimeException("Unable to create key file path " + keyFilePath.getAbsolutePath());
    }

    // This method would upload the provided key file to the provider key file path.
    public AccessKey uploadKeyFile(UUID regionUUID, File uploadedFile,
                                   String keyCode, KeyType keyType) {
        Region region = Region.get(regionUUID);
        String keyFilePath = getKeyFilePath(region.provider.uuid);
        AccessKey accessKey = AccessKey.get(region.provider.uuid, keyCode);
        if (accessKey != null) {
            throw new RuntimeException("Duplicate Access KeyCode: " + keyCode);
        }
        Path source = Paths.get(uploadedFile.getAbsolutePath());
        Path destination = Paths.get(keyFilePath, keyCode + keyType.getExtension());
        if (!Files.exists(source)) {
            throw new RuntimeException("Key file " + source.getFileName() + " not found.");
        }
        if (Files.exists(destination) ) {
            throw new RuntimeException("File " + destination.getFileName() + " already exists.");
        }

        try {
            Files.move(source, destination);
        } catch (IOException e) {
            LOG.error(e.getMessage(), e);
            throw new RuntimeException("Unable to upload key file " + source.getFileName());
        }

        String keyFileName = destination.getFileName().toString();
        AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
        if (keyType == AccessManager.KeyType.PUBLIC) {
            keyInfo.publicKey = keyFileName;
        } else {
            keyInfo.privateKey = keyFileName;
        }
        return AccessKey.create(region.provider.uuid, keyCode, keyInfo);
    }

    // This method would create a public/private key file and upload that to
    // the provider cloud account. And store the credentials file in the keyFilePath
    // and return the file names. It will also create the vault file
    public AccessKey addKey(UUID regionUUID, String keyCode) {
        List<String> command = new ArrayList<String>();
        Region region = Region.get(regionUUID);
        String keyFilePath = getKeyFilePath(region.provider.uuid);
        AccessKey accessKey = AccessKey.get(region.provider.uuid, keyCode);

        command.add("add-key");
        command.add("--key_pair_name");
        command.add(keyCode);
        command.add("--key_file_path");
        command.add(keyFilePath);

        if (accessKey != null && accessKey.getKeyInfo().privateKey != null) {
            command.add("--private_key_file");
            command.add(accessKey.getKeyInfo().privateKey);
        }

        JsonNode response = executeCommand(regionUUID, command);
        if (response.has("error")) {
            throw new RuntimeException(response.get("error").asText());
        }

        if (accessKey == null) {
            AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
            keyInfo.publicKey = response.get("public_key").asText();
            keyInfo.privateKey = response.get("private_key").asText();
            JsonNode vaultResponse = createVault(regionUUID, keyInfo.privateKey);
            if (response.has("error")) {
                throw new RuntimeException(response.get("error").asText());
            }
            keyInfo.vaultFile = vaultResponse.get("vault_file").asText();
            keyInfo.vaultPasswordFile = vaultResponse.get("vault_password").asText();
            accessKey = AccessKey.create(region.provider.uuid, keyCode, keyInfo);
        }
        return accessKey;
    }

    public JsonNode createVault(UUID regionUUID, String privateKeyFile) {
        List<String> command = new ArrayList<String>();

        if (!new File(privateKeyFile).exists()) {
            throw new RuntimeException("File " + privateKeyFile + " doesn't exists.");
        }
        command.add("create-vault");
        command.add("--private_key_file");
        command.add(privateKeyFile);
        return executeCommand(regionUUID, command);
    }
    
    public JsonNode listKeys(UUID regionUUID) {
        List<String> command = new ArrayList<String>();
        command.add("list-keys");
        return executeCommand(regionUUID, command);
    }

    private JsonNode executeCommand(UUID regionUUID, List<String> commandArgs) {
        ShellProcessHandler.ShellResponse response = execCommand(regionUUID, commandArgs);
        if (response.code == 0) {
            return Json.parse(response.message);
        } else {
            LOG.error(response.message);
            return ApiResponse.errorJSON("AccessManager failed to execute");
        }
    }
}

