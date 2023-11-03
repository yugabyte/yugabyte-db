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
package com.yugabyte.yw.common.certmgmt.providers;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateDetails;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.models.CertificateInfo;
import java.io.File;
import java.security.KeyPair;
import java.security.PrivateKey;
import java.security.cert.X509Certificate;
import java.util.Collections;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.cert.jcajce.JcaX509CertificateHolder;
import org.flywaydb.play.FileUtils;

@Slf4j
public class CertificateSelfSigned extends CertificateProviderBase {

  private final Config appConfig;

  private final CertificateHelper certificateHelper;

  X509Certificate curCaCertificate;
  KeyPair curKeyPair;

  public CertificateSelfSigned(
      UUID pCACertUUID, Config config, CertificateHelper certificateHelper) {
    super(CertConfigType.SelfSigned, pCACertUUID);
    this.appConfig = config;
    this.certificateHelper = certificateHelper;
  }

  public CertificateSelfSigned(
      CertificateInfo rootCertConfigInfo, Config config, CertificateHelper certificateHelper) {
    this(rootCertConfigInfo.getUuid(), config, certificateHelper);
  }

  @Override
  public CertificateDetails createCertificate(
      String storagePath,
      String username,
      Date certStart,
      Date certExpiry,
      String certFileName,
      String certKeyName,
      Map<String, Integer> subjectAltNames) {
    UUID rootCA = caCertUUID;

    log.info(
        "Creating signed certificate signed by root CA {} and user {} at path {}",
        rootCA,
        username,
        storagePath);

    try {
      // Add the security provider in case createSignedCertificate was never called.
      KeyPair newCertKeyPair = CertificateHelper.getKeyPairObject();
      boolean syncCertsToDB = CertificateHelper.DEFAULT_CLIENT.equals(username);

      CertificateInfo certInfo = CertificateInfo.get(rootCA);
      if (certInfo.getPrivateKey() == null) {
        throw new PlatformServiceException(BAD_REQUEST, "Keyfile cannot be null!");
      }

      // The first entry will be the certificate that needs to sign the necessary certificate.
      X509Certificate cer =
          CertificateHelper.convertStringToX509CertList(
                  FileUtils.readFileToString(new File(certInfo.getCertificate())))
              .get(0);
      X500Name subject = new JcaX509CertificateHolder(cer).getSubject();
      log.debug("Root CA Certificate is:: {}", CertificateHelper.getCertificateProperties(cer));

      PrivateKey pk;
      try {
        pk =
            CertificateHelper.getPrivateKey(
                FileUtils.readFileToString(new File(certInfo.getPrivateKey())));
      } catch (Exception e) {
        log.error(
            "Unable to create certificate for username {} using root CA {}", username, rootCA, e);
        throw new PlatformServiceException(BAD_REQUEST, "Could not create certificate");
      }

      X509Certificate newCert =
          CertificateHelper.createAndSignCertificate(
              username,
              subject,
              newCertKeyPair,
              cer,
              pk,
              subjectAltNames,
              appConfig.getInt("yb.tlsCertificate.server.maxLifetimeInYears"));
      log.info(
          "Created a certificate for username {} signed by root CA {} - {}.",
          username,
          rootCA,
          CertificateHelper.getCertificateProperties(newCert));

      return CertificateHelper.dumpNewCertsToFile(
          storagePath,
          certFileName,
          certKeyName,
          newCert,
          newCertKeyPair.getPrivate(),
          syncCertsToDB);

    } catch (Exception e) {
      log.error(
          "Unable to create certificate for username {} using root CA {}", username, rootCA, e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Could not create certificate");
    }
  }

  @Override
  public Pair<String, String> dumpCACertBundle(
      String storagePath, UUID customerUUID, UUID caCertUUIDParam) {
    // Do not make user of CertificateInfo here, it is passed as param
    String certPath = CertificateHelper.getCACertPath(storagePath, customerUUID, caCertUUIDParam);
    String keyPath = CertificateHelper.getCAKeyPath(storagePath, customerUUID, caCertUUIDParam);
    log.info("Dumping CA certs @{}", certPath);

    CertificateHelper.writeCertBundleToCertPath(
        Collections.singletonList(curCaCertificate), certPath);
    CertificateHelper.writeKeyFileContentToKeyPath(curKeyPair.getPrivate(), keyPath);
    return new ImmutablePair<>(certPath, keyPath);
  }

  @Override
  public X509Certificate generateCACertificate(String certLabel, KeyPair keyPair) throws Exception {
    log.debug("Called generateCACertificate for: {}", certLabel);
    int timeInYears = 4;
    try {
      timeInYears = appConfig.getInt("yb.tlsCertificate.root.expiryInYears");
    } catch (Exception e) {
      log.error("Failed to get yb.tlsCertificate.root.expiryInYears");
    }

    curCaCertificate = certificateHelper.generateCACertificate(certLabel, keyPair, timeInYears);
    curKeyPair = keyPair;
    return curCaCertificate;
  }
}
