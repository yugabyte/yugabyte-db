/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.kms.util.hashicorpvault;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Calendar;
import java.util.List;
import java.util.Map;
import java.util.TimeZone;

import com.bettercloud.vault.Vault;
import com.bettercloud.vault.VaultConfig;
import com.bettercloud.vault.VaultException;
import com.bettercloud.vault.response.LogicalResponse;
import com.bettercloud.vault.rest.RestResponse;
import com.bettercloud.vault.api.Auth.TokenRequest;
import com.bettercloud.vault.response.AuthResponse;

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.helpers.CommonUtils;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/*
 * Wrapper over Vault.logical crud operations
 */
public class VaultAccessor {
  public static final Logger LOG = LoggerFactory.getLogger(VaultAccessor.class);

  public static final long TTL_RENEWAL_BEFORE_EXPIRY_HRS = 24;
  public static final long TTL_EXPIRY_PERCENT_FACTOR = 7; // => x*10%, i.e., if 7 then 70%.

  private Vault vault;
  private long tokenTTL;
  private Calendar tokenTtlExpiry;
  /** Vault supports 2 versions of API, we have to use version 1 to connect with Vault. */
  private int apiVersion;

  /**
   * Constructs vault object of com.bettercloud.vault.Vault use buildVaultAccessor to build a object
   *
   * @param vObj
   * @param apiVer
   */
  public VaultAccessor(Vault vObj, int apiVer) {

    tokenTTL = 0;
    apiVersion = apiVer;
    LOG.debug("Calling HCVaultAccessor: with vault {}, API version: {}", vObj, apiVersion);

    vault = vObj;
    tokenTtlExpiry = Calendar.getInstance();
  }

  /**
   * From address(url) and from token, build Vault object to access vault.
   *
   * @param vaultAddr
   * @param vaultToken
   * @return
   * @throws VaultException
   */
  public static VaultAccessor buildVaultAccessor(String vaultAddr, String vaultToken)
      throws VaultException {
    LOG.debug("Calling buildVaultAccessor: with addr {}", vaultAddr);

    int apiversion = 1;
    VaultConfig config = new VaultConfig().address(vaultAddr).token(vaultToken).build();
    Vault vault = new Vault(config, Integer.valueOf(apiversion));
    LOG.info("Created vault connection with {}, - {}", vaultAddr, vault);
    VaultAccessor vAccessor = new VaultAccessor(vault, apiversion);

    try {
      vAccessor.tokenSelfLookupCheck();
      vAccessor.getTokenExpiryFromVault();
      vAccessor.renewSelf();
    } catch (VaultException e) {
      LOG.error("Creation of vault is failed with error:" + e.getMessage());
      throw e;
    }
    return vAccessor;
  }

  public void tokenSelfLookupCheck() throws VaultException {
    RestResponse rr = vault.auth().lookupSelf().getRestResponse();
    checkForResponseFailure(rr);
  }

  public String getMountType(String mountPath) {
    try {
      // vault read /sys/mounts/ -format=json | jq '.data."transit/".type'
      String fetchedType = vault.mounts().list().getMounts().get(mountPath).getType().toString();
      LOG.info(
          "Checking mounts for : {}. From Mounts - Secret Engine fetched type: {}",
          mountPath,
          fetchedType);

      return fetchedType;
    } catch (VaultException e) {
      LOG.debug("Cannot extract secret engine type /sys/mounts/, exception :" + e.getMessage());
    }
    return new String("INVAID");
  }

  /**
   * Checks for response and throws exception if its one of the http error codes
   *
   * @param restResp
   * @throws VaultException
   */
  public int checkForResponseFailure(RestResponse restResp) throws VaultException {
    int status = restResp.getStatus();
    LOG.debug("Response status is : {}", status);
    if (status < 400) return status;

    throw new VaultException(
        "Hashicorp Vault responded with HTTP status code: "
            + status
            + "\nResponse body: "
            + new String(restResp.getBody(), StandardCharsets.UTF_8),
        status);
  }

  /**
   * Extracts ttl from vault and sets it to the object
   *
   * @return List<long ttl, long ttlexpiry> ttl: ttl (int seconds) fetched from vault and ttlexpiry:
   *     time stamp (int milisecods) of expiry (currentTime + ttl)
   * @throws VaultException
   */
  public List<Object> getTokenExpiryFromVault() throws VaultException {
    LOG.debug("Called getTokenExpiryFromVault");

    tokenTTL = vault.auth().lookupSelf().getTTL();
    String token = vault.auth().lookupSelf().getId();

    tokenTtlExpiry = Calendar.getInstance();

    if (tokenTTL == 0) {
      LOG.info("The token {} will never expire", CommonUtils.getMaskedValue("TOKEN", token));
      return Arrays.asList(tokenTTL, tokenTtlExpiry.getTimeInMillis());
    }

    tokenTtlExpiry.add(Calendar.SECOND, (int) tokenTTL);

    String ttlInfo =
        Util.unixTimeToDateString(
            System.currentTimeMillis(), "yyyy-MM-dd HH:mm:ss", TimeZone.getTimeZone("UTC"));

    String remainTime =
        String.format(
            " [DD::HH:mm:ss] %02d::%02d:%02d:%02d",
            tokenTTL / (24 * 3600),
            (tokenTTL % (24 * 3600)) / 3600,
            (tokenTTL / 60) % 60,
            tokenTTL % 60);

    LOG.info(
        "The token {} will expire at: {} with countdown of: {} ",
        CommonUtils.getMaskedValue("TOKEN", token),
        ttlInfo,
        remainTime);
    return Arrays.asList(tokenTTL, tokenTtlExpiry.getTimeInMillis());
  }

  /** This is done as best effort, and no gurantees are given at this point of time. */
  public void renewSelf() {
    try {
      boolean isRenewable = vault.auth().lookupSelf().isRenewable();
      String token = vault.auth().lookupSelf().getId();
      token = CommonUtils.getMaskedValue("TOKEN", token);
      if (!isRenewable) {
        LOG.warn("Vault Token {} is not renewable", token);
        return;
      }

      long ttl = vault.auth().lookupSelf().getTTL();
      if (ttl < (3600 * TTL_RENEWAL_BEFORE_EXPIRY_HRS)) {
        vault.auth().renewSelf();
        LOG.info("Token {} is renewed as it was very close to expiry", token);
        return;
      }

      long maxttl = vault.auth().lookupSelf().getCreationTTL();
      if ((ttl * TTL_EXPIRY_PERCENT_FACTOR) < maxttl) {
        vault.auth().renewSelf();
        LOG.info("Token {} is renewed as it has passed 90% of its expiry window", token);
      } else LOG.debug("Not need to renew token {} for now", token);

    } catch (VaultException e) {
      LOG.warn("Received exception while attempting to renew");
    }
  }

  /** currently used for testing purpose only */
  public String createToken(String ttl) throws VaultException {
    TokenRequest tr = new TokenRequest();
    if (!"".equals(ttl)) tr.ttl(ttl);
    AuthResponse ar = vault.auth().createToken(tr);
    return ar.getAuthClientToken();
  }
  /**
   * Performs list operation on provided path of secret engine.
   *
   * @param path
   * @return
   * @throws VaultException
   */
  public String listAt(String path) throws VaultException {
    LOG.info("ListAt called : {}", path);

    LogicalResponse logicalResp = vault.logical().list(path);
    checkForResponseFailure(logicalResp.getRestResponse());
    return logicalResp.getData().toString();
  }

  /**
   * Performs GET operation on path provided
   *
   * @param path
   * @param filter
   * @return
   * @throws VaultException
   */
  public String readAt(String path, String filter) throws VaultException {
    LOG.info("ReadAt called : {}", path);
    LogicalResponse logicalResp = vault.logical().read(path);
    checkForResponseFailure(logicalResp.getRestResponse());

    if ("".equals(filter)) return logicalResp.getData().toString();
    else return logicalResp.getDataObject().get(filter).toString();
  }

  /**
   * Performs POST operation on path provided
   *
   * @param path
   * @param textmap dictionary of data that is getting posted.
   * @param filter to return specific vaule of output
   * @return
   * @throws VaultException
   */
  public String writeAt(String path, Map<String, Object> textmap, String filter)
      throws VaultException {
    LOG.info("WriteAt called : {}", path);
    LogicalResponse logicalResp = vault.logical().write(path, textmap);
    checkForResponseFailure(logicalResp.getRestResponse());

    if ("".equals(filter)) return logicalResp.getData().toString();
    else return logicalResp.getData().get(filter).toString();
  }
  /**
   * Performs POST operation on path provided
   *
   * @param path
   * @param textmap dictionary of data that is getting posted.
   * @return returns whole response parameters in form of Map
   * @throws VaultException
   */
  public Map<String, String> writeAt(String path, Map<String, Object> textmap)
      throws VaultException {
    LOG.info("WriteAt called : {}", path);
    LogicalResponse logicalResp = vault.logical().write(path, textmap);
    checkForResponseFailure(logicalResp.getRestResponse());

    return logicalResp.getData();
  }

  /**
   * Performs HTTP DELETE operation on path provided
   *
   * @param path
   * @return
   * @throws VaultException
   */
  public String deleteAt(String path) throws VaultException {
    LOG.info("DeleteAt called : {}", path);
    LogicalResponse logicalResp = vault.logical().delete(path);
    checkForResponseFailure(logicalResp.getRestResponse());

    return logicalResp.getData().toString();
  }
}
