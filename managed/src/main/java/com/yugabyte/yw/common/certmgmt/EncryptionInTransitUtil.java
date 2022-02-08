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

import java.security.cert.X509Certificate;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.io.File;

import com.google.api.client.util.Strings;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.providers.CertificateProviderBase;
import com.yugabyte.yw.common.certmgmt.providers.CertificateSelfSigned;
import com.yugabyte.yw.common.certmgmt.providers.VaultPKI;
import com.yugabyte.yw.common.kms.util.hashicorpvault.HashicorpVaultConfigParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.CertificateInfo;

import org.apache.commons.lang3.tuple.Pair;
import org.flywaydb.play.FileUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class EncryptionInTransitUtil {
  public static final Logger LOG = LoggerFactory.getLogger(EncryptionInTransitUtil.class);

  public static CertificateProviderBase getCertificateProviderInstance(CertificateInfo info) {
    CertificateProviderBase certProvider = null;
    try {
      switch (info.certType) {
        case HashicorpVault:
          certProvider = VaultPKI.getVaultPKIInstance(info);
          break;
        case SelfSigned:
          certProvider = new CertificateSelfSigned(info);
          break;
        default:
          throw new PlatformServiceException(
              BAD_REQUEST, "Certificate config type mismatch in createClientCertificate");
      }
    } catch (Exception e) {
      String message = "Cannot create certificate. " + e.toString();
      throw new PlatformServiceException(BAD_REQUEST, message);
    }
    LOG.debug(
        "Returning from getCertificateProviderInstance type is: {}", info.certType.toString());
    return certProvider;
  }

  public static void fetchLatestCAForHashicorpPKI(CertificateInfo info, String storagePath)
      throws Exception {
    UUID custUUID = info.customerUUID;
    CertificateProviderBase provider = getCertificateProviderInstance(info);
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
          String.format(
              "Hashicorp Vault parameters provided are not valid - %s", hcVaultParams.toString());
      throw new PlatformServiceException(BAD_REQUEST, message);
    }

    UUID rootCA_UUID = UUID.randomUUID();
    VaultPKI pkiObjValidator = VaultPKI.validateVaultConfigParams(hcVaultParams);
    Pair<String, String> paths =
        pkiObjValidator.dumpCACertBundle(storagePath, customerUUID, rootCA_UUID);

    Pair<Date, Date> dates =
        CertificateHelper.extractDatesFromCertBundle(
            CertificateHelper.convertStringToX509CertList(
                FileUtils.readFileToString(new File(paths.getLeft()))));

    CertificateInfo cert =
        CertificateInfo.create(
            rootCA_UUID,
            customerUUID,
            label,
            dates.getLeft(),
            dates.getRight(),
            paths.getLeft(),
            hcVaultParams);

    LOG.info("Created Root CA for universe {}.", label);

    if (!CertificateInfo.isCertificateValid(rootCA_UUID)) {
      String errMsg =
          String.format("The certificate %s needs info. Update the cert and retry.", label);
      LOG.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    return cert.uuid;
  }

  public static void editEITHashicorpConfig(
      UUID caCertUUID, UUID customerUUID, String storagePath, HashicorpVaultConfigParams params) {

    try {
      VaultPKI pkiObjValidator = VaultPKI.validateVaultConfigParams(params);
      // call dumpCACertBundle which take caCertUUID.
      Pair<String, String> certPath =
          pkiObjValidator.dumpCACertBundle(storagePath, customerUUID, caCertUUID);

      Pair<Date, Date> dates =
          CertificateHelper.extractDatesFromCertBundle(
              CertificateHelper.convertStringToX509CertList(
                  FileUtils.readFileToString(new File(certPath.getLeft()))));

      LOG.info("Updating table with ca certificate: {}", certPath.getLeft());

      CertificateInfo rootCertConfigInfo = CertificateInfo.get(caCertUUID);
      rootCertConfigInfo.update(dates.getLeft(), dates.getRight(), certPath.getLeft(), params);

    } catch (Exception e) {
      String message =
          "Error occured while attempting to edit Hashicorp certificate config settings:"
              + e.getMessage();
      throw new PlatformServiceException(BAD_REQUEST, message);
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
    return enableNodeToNodeEncrypt || (rootAndClientRootCASame && enableClientToNodeEncrypt);
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
    return !rootAndClientRootCASame && enableClientToNodeEncrypt;
  }
}
