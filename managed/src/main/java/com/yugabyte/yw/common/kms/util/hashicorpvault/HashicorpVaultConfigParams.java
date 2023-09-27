/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use info file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util.hashicorpvault;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.models.helpers.CommonUtils;
import io.swagger.annotations.ApiModelProperty;
import java.text.SimpleDateFormat;
import java.util.Calendar;
import java.util.HashMap;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Represents params for Hashicorp Vault config (EncryptionAtTransit) */
public class HashicorpVaultConfigParams {
  public static final Logger LOG = LoggerFactory.getLogger(HashicorpVaultConfigParams.class);

  // Params sent from UI - added to the auth config (both EAR and EAT)
  public static final String HC_VAULT_TOKEN = "HC_VAULT_TOKEN";
  public static final String HC_VAULT_ADDRESS = "HC_VAULT_ADDRESS";
  public static final String HC_VAULT_ENGINE = "HC_VAULT_ENGINE";
  public static final String HC_VAULT_MOUNT_PATH = "HC_VAULT_MOUNT_PATH";
  public static final String HC_VAULT_KEY_NAME = "HC_VAULT_KEY_NAME";

  // AppRole credentials
  public static final String HC_VAULT_ROLE_ID = "HC_VAULT_ROLE_ID";
  public static final String HC_VAULT_SECRET_ID = "HC_VAULT_SECRET_ID";

  // Optional method currently used for AppRole authentication. Can be extended to other
  // authentication methods
  public static final String HC_VAULT_AUTH_NAMESPACE = "HC_VAULT_AUTH_NAMESPACE";

  // Params sent from UI - added to the auth config (Only EAT)
  public static final String HC_VAULT_PKI_ROLE = "HC_VAULT_PKI_ROLE";

  // Extra params added to the auth config (both EAR and EAT)
  public static final String HC_VAULT_TTL = "HC_VAULT_TTL";
  public static final String HC_VAULT_TTL_EXPIRY = "HC_VAULT_TTL_EXPIRY";

  public String vaultAddr;
  public String engine;
  public String mountPath;

  @ApiModelProperty(required = false)
  public String vaultToken;

  @ApiModelProperty(required = false)
  public String vaultRoleID;

  @ApiModelProperty(required = false)
  public String vaultSecretID;

  @ApiModelProperty(required = false)
  public String vaultAuthNamespace;

  // @ApiModelProperty(required = false)
  public String role;

  @ApiModelProperty(required = false)
  public long ttl;

  @ApiModelProperty(required = false)
  public long ttlExpiry;

  public HashicorpVaultConfigParams() {}

  public HashicorpVaultConfigParams(HashicorpVaultConfigParams p2) {
    vaultAddr = p2.vaultAddr;
    vaultToken = p2.vaultToken;
    vaultRoleID = p2.vaultRoleID;
    vaultSecretID = p2.vaultSecretID;
    engine = p2.engine;
    mountPath = p2.mountPath;
    role = p2.role;
    ttl = p2.ttl;
    ttlExpiry = p2.ttlExpiry;
    vaultAuthNamespace = p2.vaultAuthNamespace;
  }

  public HashicorpVaultConfigParams(JsonNode node) {
    ObjectMapper mapper = new ObjectMapper();
    Map<String, String> map =
        mapper.convertValue(node, new TypeReference<Map<String, String>>() {});
    // Map<String, String> map = mapper.convertValue(node, Map.class);
    vaultAddr = map.get(HC_VAULT_ADDRESS);
    vaultToken = map.get(HC_VAULT_TOKEN);
    vaultRoleID = map.get(HC_VAULT_ROLE_ID);
    vaultSecretID = map.get(HC_VAULT_SECRET_ID);
    engine = map.get(HC_VAULT_ENGINE);
    mountPath = map.get(HC_VAULT_MOUNT_PATH);
    role = map.get(HC_VAULT_PKI_ROLE);
    vaultAuthNamespace = map.get(HC_VAULT_AUTH_NAMESPACE);
  }

  public String toString() {
    String result = "";

    result += String.format(" Vault Address:%s", vaultAddr);
    result += String.format(" Vault token:%s", CommonUtils.getMaskedValue("TOKEN", vaultToken));
    result += String.format(" Vault roleID:%s", CommonUtils.getMaskedValue("ROLEID", vaultRoleID));
    result +=
        String.format(" Vault secretID:%s", CommonUtils.getMaskedValue("SECRETID", vaultSecretID));
    result += String.format(" Vault Engine:%s", engine);
    result += String.format(" Vault path:%s", mountPath);
    result += String.format(" Vault Auth Namespace:%s", vaultAuthNamespace);
    result += String.format(" Vault role:%s", role);
    result += String.format(" ttl:%s", ttl);

    if (ttl == 0) result += "ttlExpiry: never";
    else {
      Calendar ttlExpiryDate = Calendar.getInstance();
      ttlExpiryDate.setTimeInMillis(ttlExpiry);
      String dateStr =
          new SimpleDateFormat("yyyy/MM/dd HH:mm:ss.SSS").format(ttlExpiryDate.getTime());
      result += String.format("ttlExpiry: %s", dateStr);
    }
    return result;
  }

  public Map<String, String> toMap() {
    Map<String, String> map = new HashMap<>();
    map.put(HC_VAULT_ADDRESS, vaultAddr);
    map.put(HC_VAULT_TOKEN, vaultToken);
    map.put(HC_VAULT_ROLE_ID, vaultRoleID);
    map.put(HC_VAULT_SECRET_ID, vaultSecretID);
    map.put(HC_VAULT_ENGINE, engine);
    map.put(HC_VAULT_MOUNT_PATH, mountPath);
    map.put(HC_VAULT_AUTH_NAMESPACE, vaultAuthNamespace);
    map.put(HC_VAULT_PKI_ROLE, role);
    map.put(HC_VAULT_TTL, String.valueOf(ttl));
    map.put(HC_VAULT_TTL_EXPIRY, String.valueOf(ttlExpiry));
    return map;
  }

  public JsonNode toJsonNode() {
    try {
      ObjectMapper mapper = new ObjectMapper();
      JsonNode obj = mapper.valueToTree(toMap());

      return obj;
    } catch (Exception e) {
      LOG.error("Error occured while preparing updated HashicorpVaultConfigParams");
    }
    return null;
  }
}
