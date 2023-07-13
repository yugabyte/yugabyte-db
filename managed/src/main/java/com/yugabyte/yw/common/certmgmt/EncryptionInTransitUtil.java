/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/
 *  POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common.certmgmt;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.api.client.util.Strings;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.providers.CertificateProviderBase;
import com.yugabyte.yw.common.certmgmt.providers.CertificateSelfSigned;
import com.yugabyte.yw.common.certmgmt.providers.VaultPKI;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.CertificateInfo;
import java.io.File;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.Pair;
import org.flywaydb.play.FileUtils;
import org.springframework.security.crypto.encrypt.Encryptors;
import org.springframework.security.crypto.encrypt.TextEncryptor;

@Slf4j
public class EncryptionInTransitUtil {
  public static final String SALT_STR = "hashicorpcert";

  @Inject static RuntimeConfGetter runtimeConfGetter;

  public static CertificateProviderBase getCertificateProviderInstance(
      CertificateInfo info, Config config) {
    CertificateProviderBase certProvider;
    try {
      switch (info.getCertType()) {
        case HashicorpVault:
          certProvider = VaultPKI.getVaultPKIInstance(info);
          break;
        case SelfSigned:
          certProvider =
              new CertificateSelfSigned(info, config, new CertificateHelper(runtimeConfGetter));
          break;
        default:
          throw new PlatformServiceException(
              BAD_REQUEST, "Certificate config type mismatch in createClientCertificate");
      }
    } catch (Exception e) {
      String message = "Cannot create certificate. " + e;
      throw new PlatformServiceException(BAD_REQUEST, message);
    }
    log.debug(
        "Returning from getCertificateProviderInstance type is: {}", info.getCertType().toString());
    return certProvider;
  }

  public static void fetchLatestCAForHashicorpPKI(CertificateInfo info, Config config)
      throws Exception {
    UUID custUUID = info.getCustomerUUID();
    String storagePath = AppConfigHelper.getStoragePath();
    CertificateProviderBase provider = getCertificateProviderInstance(info, config);
    provider.dumpCACertBundle(storagePath, custUUID);
  }

  public static UUID createHashicorpCAConfig(
      UUID customerUUID, String label, String storagePath, HashicorpVaultConfigParams hcVaultParams)
      throws Exception {
    if (Strings.isNullOrEmpty(hcVaultParams.vaultAddr)
        || Strings.isNullOrEmpty(hcVaultParams.vaultToken)
        || Strings.isNullOrEmpty(hcVaultParams.engine)
        || Strings.isNullOrEmpty(hcVaultParams.mountPath)
        || Strings.isNullOrEmpty(hcVaultParams.role)) {
      String message =
          String.format("Hashicorp Vault parameters provided are not valid - %s", hcVaultParams);
      throw new PlatformServiceException(BAD_REQUEST, message);
    }

    UUID certConfigUUID = UUID.randomUUID();
    VaultPKI pkiObjValidator = VaultPKI.validateVaultConfigParams(hcVaultParams);
    Pair<String, String> paths =
        pkiObjValidator.dumpCACertBundle(storagePath, customerUUID, certConfigUUID);

    Pair<Date, Date> dates =
        CertificateHelper.extractDatesFromCertBundle(
            CertificateHelper.convertStringToX509CertList(
                FileUtils.readFileToString(new File(paths.getLeft()))));

    hcVaultParams.vaultToken = maskCertConfigData(customerUUID, hcVaultParams.vaultToken);
    CertificateInfo cert =
        CertificateInfo.create(
            certConfigUUID,
            customerUUID,
            label,
            dates.getLeft(),
            dates.getRight(),
            paths.getLeft(),
            hcVaultParams);

    log.info("Created Root CA for universe {}.", label);

    if (!CertificateInfo.isCertificateValid(certConfigUUID)) {
      String errMsg =
          String.format("The certificate %s needs info. Update the cert and retry.", label);
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    List<Object> ttlInfo = pkiObjValidator.getTTL();
    hcVaultParams.ttl = (long) ttlInfo.get(0);
    hcVaultParams.ttlExpiry = (long) ttlInfo.get(1);

    CertificateInfo rootCertConfigInfo = CertificateInfo.get(certConfigUUID);
    rootCertConfigInfo.update(dates.getLeft(), dates.getRight(), paths.getLeft(), hcVaultParams);

    return cert.getUuid();
  }

  public static void editEITHashicorpConfig(
      UUID configCertUUIDparam,
      UUID customerUUID,
      String storagePath,
      HashicorpVaultConfigParams hcVparams) {

    try {
      VaultPKI pkiObjValidator = VaultPKI.validateVaultConfigParams(hcVparams);
      // call dumpCACertBundle which take configCertUUIDparam.
      Pair<String, String> certPath =
          pkiObjValidator.dumpCACertBundle(storagePath, customerUUID, configCertUUIDparam);

      Pair<Date, Date> dates =
          CertificateHelper.extractDatesFromCertBundle(
              CertificateHelper.convertStringToX509CertList(
                  FileUtils.readFileToString(new File(certPath.getLeft()))));

      log.info("Updating table with ca certificate: {}", certPath.getLeft());

      List<Object> ttlInfo = pkiObjValidator.getTTL();
      hcVparams.ttl = (long) ttlInfo.get(0);
      hcVparams.ttlExpiry = (long) ttlInfo.get(1);
      hcVparams.vaultToken = maskCertConfigData(customerUUID, hcVparams.vaultToken);

      CertificateInfo rootCertConfigInfo = CertificateInfo.get(configCertUUIDparam);
      rootCertConfigInfo.update(dates.getLeft(), dates.getRight(), certPath.getLeft(), hcVparams);

    } catch (Exception e) {
      String message =
          "Error occured while attempting to edit Hashicorp certificate config settings:"
              + e.getMessage();
      throw new PlatformServiceException(BAD_REQUEST, message);
    }
  }

  public static String getSaltHashForCert(UUID customerUUID) {

    String salt =
        String.format(
            "%s%s",
            customerUUID.toString().replace("-", ""),
            String.valueOf(SALT_STR.hashCode()).replace("-", ""));

    if ((salt.length() % 2) != 0) salt += "0";
    return salt;
  }

  public static String maskCertConfigData(UUID customerUUID, String data) {
    try {
      String salt = getSaltHashForCert(customerUUID);

      final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);
      return encryptor.encrypt(data);
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Could not mask CertificateConfig for customer %s", customerUUID.toString());
      log.error(errMsg, e);
      return null;
    }
  }

  public static String unmaskCertConfigData(UUID customerUUID, String data) {

    if (Strings.isNullOrEmpty(data)) return data;

    try {
      String salt = getSaltHashForCert(customerUUID);
      final TextEncryptor encryptor = Encryptors.delux(customerUUID.toString(), salt);

      return encryptor.decrypt(data);
    } catch (Exception e) {
      final String errMsg =
          String.format(
              "Could not decrypt Cert configuration for customer %s", customerUUID.toString());
      log.error(errMsg, e);
      return null;
    }
  }

  public static boolean isRootCARequired(UserIntent userIntent, boolean rootAndClientRootCASame) {
    return isRootCARequired(
        userIntent.enableNodeToNodeEncrypt,
        userIntent.enableClientToNodeEncrypt,
        rootAndClientRootCASame);
  }

  public static boolean isRootCARequired(UniverseDefinitionTaskParams taskParams) {
    UserIntent userIntent = taskParams.getPrimaryCluster().userIntent;
    return isRootCARequired(
        userIntent.enableNodeToNodeEncrypt,
        userIntent.enableClientToNodeEncrypt,
        taskParams.rootAndClientRootCASame);
  }

  public static boolean isRootCARequired(AnsibleConfigureServers.Params taskParams) {
    return isRootCARequired(
        taskParams.enableNodeToNodeEncrypt,
        taskParams.enableClientToNodeEncrypt,
        taskParams.rootAndClientRootCASame);
  }

  public static boolean isRootCARequired(UniverseSetTlsParams.Params taskParams) {
    return isRootCARequired(
        taskParams.enableNodeToNodeEncrypt,
        taskParams.enableClientToNodeEncrypt,
        taskParams.rootAndClientRootCASame);
  }

  public static boolean isRootCARequired(TlsToggleParams taskParams) {
    return isRootCARequired(
        taskParams.enableNodeToNodeEncrypt,
        taskParams.enableClientToNodeEncrypt,
        taskParams.rootAndClientRootCASame);
  }

  public static boolean isRootCARequired(
      boolean enableNodeToNodeEncrypt,
      boolean enableClientToNodeEncrypt,
      boolean rootAndClientRootCASame) {
    return enableNodeToNodeEncrypt;
  }

  public static boolean isClientRootCARequired(
      UserIntent userIntent, boolean rootAndClientRootCASame) {
    return isClientRootCARequired(
        userIntent.enableNodeToNodeEncrypt,
        userIntent.enableClientToNodeEncrypt,
        rootAndClientRootCASame);
  }

  public static boolean isClientRootCARequired(UniverseDefinitionTaskParams taskParams) {
    UserIntent userIntent = taskParams.getPrimaryCluster().userIntent;
    return isClientRootCARequired(
        userIntent.enableNodeToNodeEncrypt,
        userIntent.enableClientToNodeEncrypt,
        taskParams.rootAndClientRootCASame);
  }

  public static boolean isClientRootCARequired(AnsibleConfigureServers.Params taskParams) {
    return isClientRootCARequired(
        taskParams.enableNodeToNodeEncrypt,
        taskParams.enableClientToNodeEncrypt,
        taskParams.rootAndClientRootCASame);
  }

  public static boolean isClientRootCARequired(UniverseSetTlsParams.Params taskParams) {
    return isClientRootCARequired(
        taskParams.enableNodeToNodeEncrypt,
        taskParams.enableClientToNodeEncrypt,
        taskParams.rootAndClientRootCASame);
  }

  public static boolean isClientRootCARequired(TlsToggleParams taskParams) {
    return isClientRootCARequired(
        taskParams.enableNodeToNodeEncrypt,
        taskParams.enableClientToNodeEncrypt,
        taskParams.rootAndClientRootCASame);
  }

  public static boolean isClientRootCARequired(
      boolean enableNodeToNodeEncrypt,
      boolean enableClientToNodeEncrypt,
      boolean rootAndClientRootCASame) {
    return enableClientToNodeEncrypt;
  }
}
