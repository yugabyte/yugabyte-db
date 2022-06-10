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

import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.certmgmt.CertificateDetails;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.models.CertificateInfo;
import java.io.File;
import java.math.BigInteger;
import java.security.KeyPair;
import java.security.PrivateKey;
import java.security.cert.X509Certificate;
import java.util.Calendar;
import java.util.Collections;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x500.X500NameBuilder;
import org.bouncycastle.asn1.x500.style.BCStyle;
import org.bouncycastle.asn1.x509.BasicConstraints;
import org.bouncycastle.asn1.x509.Extension;
import org.bouncycastle.asn1.x509.GeneralNames;
import org.bouncycastle.asn1.x509.KeyUsage;
import org.bouncycastle.cert.X509CertificateHolder;
import org.bouncycastle.cert.X509v3CertificateBuilder;
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter;
import org.bouncycastle.cert.jcajce.JcaX509CertificateHolder;
import org.bouncycastle.cert.jcajce.JcaX509ExtensionUtils;
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder;
import org.bouncycastle.jce.provider.BouncyCastleProvider;
import org.bouncycastle.operator.ContentSigner;
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder;
import org.bouncycastle.pkcs.PKCS10CertificationRequest;
import org.bouncycastle.pkcs.PKCS10CertificationRequestBuilder;
import org.bouncycastle.pkcs.jcajce.JcaPKCS10CertificationRequestBuilder;
import org.flywaydb.play.FileUtils;

@Slf4j
public class CertificateSelfSigned extends CertificateProviderBase {

  @Inject Config appConfig;

  X509Certificate curCaCertificate;
  KeyPair curKeyPair;

  public CertificateSelfSigned(UUID pCACertUUID) {
    super(CertConfigType.SelfSigned, pCACertUUID);
  }

  public CertificateSelfSigned(CertificateInfo rootCertConfigInfo) {
    super(CertConfigType.SelfSigned, rootCertConfigInfo.uuid);
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

      CertificateInfo certInfo = CertificateInfo.get(rootCA);
      if (certInfo.privateKey == null) {
        throw new PlatformServiceException(BAD_REQUEST, "Keyfile cannot be null!");
      }
      // The first entry will be the certificate that needs to sign the necessary certificate.
      X509Certificate cer =
          CertificateHelper.convertStringToX509CertList(
                  FileUtils.readFileToString(new File(certInfo.certificate)))
              .get(0);
      X500Name subject = new JcaX509CertificateHolder(cer).getSubject();
      log.debug("Root CA Certificate is:: {}", CertificateHelper.getCertificateProperties(cer));

      PrivateKey pk;
      try {
        pk =
            CertificateHelper.getPrivateKey(
                FileUtils.readFileToString(new File(certInfo.privateKey)));
      } catch (Exception e) {
        log.error(
            "Unable to create certificate for username {} using root CA {}", username, rootCA, e);
        throw new PlatformServiceException(BAD_REQUEST, "Could not create certificate");
      }

      X509Certificate newCert =
          createAndSignCertificate(username, subject, newCertKeyPair, cer, pk, subjectAltNames);
      log.info(
          "Created a certificate for username {} signed by root CA {} - {}.",
          username,
          rootCA,
          CertificateHelper.getCertificateProperties(newCert));

      return CertificateHelper.dumpNewCertsToFile(
          storagePath, certFileName, certKeyName, newCert, newCertKeyPair.getPrivate());

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
    Calendar cal = Calendar.getInstance();
    Date certStart = cal.getTime();
    int timeInYears = 4;
    try {
      timeInYears = appConfig.getInt("yb.tlsCertificate.expiryInYears");
    } catch (Exception e) {
      log.error("Failed to get yb.tlsCertificate.expiryInYears");
    }
    cal.add(Calendar.YEAR, timeInYears);
    Date certExpiry = cal.getTime();

    X500Name subject =
        new X500NameBuilder(BCStyle.INSTANCE)
            .addRDN(BCStyle.CN, certLabel)
            .addRDN(BCStyle.O, "example.com")
            .build();
    BigInteger serial = BigInteger.valueOf(System.currentTimeMillis());
    X509v3CertificateBuilder certGen =
        new JcaX509v3CertificateBuilder(
            subject, serial, certStart, certExpiry, subject, keyPair.getPublic());
    BasicConstraints basicConstraints = new BasicConstraints(1);
    KeyUsage keyUsage =
        new KeyUsage(
            KeyUsage.digitalSignature
                | KeyUsage.nonRepudiation
                | KeyUsage.keyEncipherment
                | KeyUsage.keyCertSign);

    certGen.addExtension(Extension.basicConstraints, true, basicConstraints.toASN1Primitive());
    certGen.addExtension(Extension.keyUsage, true, keyUsage.toASN1Primitive());
    ContentSigner signer =
        new JcaContentSignerBuilder(CertificateHelper.SIGNATURE_ALGO).build(keyPair.getPrivate());
    X509CertificateHolder holder = certGen.build(signer);
    JcaX509CertificateConverter converter = new JcaX509CertificateConverter();
    converter.setProvider(new BouncyCastleProvider());
    X509Certificate x509 = converter.getCertificate(holder);

    curCaCertificate = x509;
    curKeyPair = keyPair;

    return x509;
  }

  public X509Certificate createAndSignCertificate(
      String username,
      X500Name subject,
      KeyPair newCertKeyPair,
      X509Certificate caCert,
      PrivateKey pk,
      Map<String, Integer> subjectAltNames)
      throws Exception {
    log.debug("Called createAndSignCertificate for: {}, {}", username, subject);
    X500Name newCertSubject = new X500Name(String.format("CN=%s", username));
    BigInteger newCertSerial = BigInteger.valueOf(System.currentTimeMillis());
    PKCS10CertificationRequestBuilder p10Builder =
        new JcaPKCS10CertificationRequestBuilder(newCertSubject, newCertKeyPair.getPublic());
    ContentSigner csrContentSigner =
        new JcaContentSignerBuilder(CertificateHelper.SIGNATURE_ALGO).build(pk);
    PKCS10CertificationRequest csr = p10Builder.build(csrContentSigner);

    KeyUsage keyUsage =
        new KeyUsage(
            KeyUsage.digitalSignature
                | KeyUsage.nonRepudiation
                | KeyUsage.keyEncipherment
                | KeyUsage.keyCertSign);

    X509v3CertificateBuilder newCertBuilder =
        new X509v3CertificateBuilder(
            subject,
            newCertSerial,
            caCert.getNotBefore(),
            caCert.getNotAfter(),
            csr.getSubject(),
            csr.getSubjectPublicKeyInfo());
    JcaX509ExtensionUtils newCertExtUtils = new JcaX509ExtensionUtils();
    newCertBuilder.addExtension(
        Extension.basicConstraints, true, new BasicConstraints(false).toASN1Primitive());
    newCertBuilder.addExtension(
        Extension.authorityKeyIdentifier,
        false,
        newCertExtUtils.createAuthorityKeyIdentifier(caCert));
    newCertBuilder.addExtension(
        Extension.subjectKeyIdentifier,
        false,
        newCertExtUtils.createSubjectKeyIdentifier(csr.getSubjectPublicKeyInfo()));
    newCertBuilder.addExtension(Extension.keyUsage, false, keyUsage.toASN1Primitive());

    GeneralNames generalNames = CertificateHelper.extractGeneralNames(subjectAltNames);
    if (generalNames != null)
      newCertBuilder.addExtension(Extension.subjectAlternativeName, false, generalNames);

    X509CertificateHolder newCertHolder = newCertBuilder.build(csrContentSigner);
    X509Certificate newCert =
        new JcaX509CertificateConverter()
            .setProvider(new BouncyCastleProvider())
            .getCertificate(newCertHolder);

    newCert.verify(caCert.getPublicKey(), "BC");

    return newCert;
  }
}
