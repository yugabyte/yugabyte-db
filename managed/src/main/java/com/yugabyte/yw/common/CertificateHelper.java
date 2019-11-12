// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.models.CertificateInfo;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x500.X500NameBuilder;
import org.bouncycastle.asn1.x500.style.BCStyle;
import org.bouncycastle.asn1.x509.BasicConstraints;
import org.bouncycastle.asn1.x509.Extension;
import org.bouncycastle.asn1.x509.KeyUsage;
import org.bouncycastle.cert.X509CertificateHolder;
import org.bouncycastle.cert.X509v3CertificateBuilder;
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter;
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder;
import org.bouncycastle.jce.provider.BouncyCastleProvider;
import org.bouncycastle.openssl.jcajce.JcaPEMWriter;
import org.bouncycastle.operator.ContentSigner;
import org.bouncycastle.operator.OperatorCreationException;
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder;
import org.flywaydb.play.FileUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.nio.file.Files;
import java.math.BigInteger;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.NoSuchAlgorithmException;
import java.security.Security;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.Base64;
import java.util.Calendar;
import java.util.Date;
import java.util.UUID;


/**
 * Helper class for Certificates
 */

public class CertificateHelper {

  public static final Logger LOG = LoggerFactory.getLogger(CertificateHelper.class);

  public static UUID createRootCA(String nodePrefix, UUID customerUUID, String storagePath) {
      try {
        Security.addProvider(new BouncyCastleProvider());
        KeyPairGenerator keypairGen = KeyPairGenerator.getInstance("RSA");
        keypairGen.initialize(2048);
        UUID rootCA_UUID = UUID.randomUUID();
        KeyPair keyPair = keypairGen.generateKeyPair();
        Calendar cal = Calendar.getInstance();
        Date certStart = cal.getTime();
        cal.add(Calendar.YEAR, 1);
        Date certExpiry = cal.getTime();
        X500Name subject = new X500NameBuilder(BCStyle.INSTANCE)
          .addRDN(BCStyle.CN, nodePrefix)
          .addRDN(BCStyle.O, "example.com")
          .build();
        BigInteger serial = BigInteger.valueOf(System.currentTimeMillis());
        X509v3CertificateBuilder certGen = new JcaX509v3CertificateBuilder(
          subject,
          serial,
          certStart,
          certExpiry,
          subject,
          keyPair.getPublic());
        BasicConstraints basicConstraints = new BasicConstraints(1);
        KeyUsage keyUsage = new KeyUsage(KeyUsage.digitalSignature | KeyUsage.nonRepudiation | KeyUsage.keyEncipherment | KeyUsage.keyCertSign);
        certGen.addExtension(
          Extension.basicConstraints,
          true,
          basicConstraints.toASN1Primitive());
        certGen.addExtension(
          Extension.keyUsage,
          true,
          keyUsage.toASN1Primitive());
        ContentSigner signer = new JcaContentSignerBuilder("SHA256withRSA")
          .build(keyPair.getPrivate());
        X509CertificateHolder holder = certGen.build(signer);
        JcaX509CertificateConverter converter = new JcaX509CertificateConverter();
        converter.setProvider(new BouncyCastleProvider());
        X509Certificate x509 = converter.getCertificate(holder);
        String certPath = String.format("%s/certs/%s/%s/ca.root.crt", storagePath, customerUUID.toString(), rootCA_UUID.toString());
        String keyPath = String.format("%s/certs/%s/%s/ca.key.pem", storagePath, customerUUID.toString(), rootCA_UUID.toString());
        File certfile = new File(certPath);
        certfile.getParentFile().mkdirs();
        File keyfile = new File(keyPath);
        JcaPEMWriter certWriter = new JcaPEMWriter(new FileWriter(certfile));
        JcaPEMWriter keyWriter = new JcaPEMWriter(new FileWriter(keyfile));
        certWriter.writeObject(x509);
        certWriter.flush();
        keyWriter.writeObject(keyPair.getPrivate());
        keyWriter.flush();
        CertificateInfo cert = CertificateInfo.create(rootCA_UUID, customerUUID, nodePrefix, certStart, certExpiry, keyPath, certPath);
        LOG.info("Created Root CA for {}.", nodePrefix);
        return cert.uuid;
      } catch (NoSuchAlgorithmException | IOException | OperatorCreationException | CertificateException e) {
        LOG.error("Unable to create RootCA for universe " + nodePrefix, e);
        return null;
      }
  }

  public static UUID uploadRootCA(String label, UUID customerUUID, String storagePath, String certContent,
                                  String keyContent, Date certStart, Date certExpiry) throws IOException {
    if (certContent == null || keyContent == null) {
      throw new RuntimeException("Keyfile or certfile can't be null");
    }
    UUID rootCA_UUID = UUID.randomUUID();
    String keyPath = String.format("%s/certs/%s/%s/ca.key.pem", storagePath, customerUUID.toString(), rootCA_UUID.toString());
    String certPath = String.format("%s/certs/%s/%s/ca.root.crt", storagePath, customerUUID.toString(), rootCA_UUID.toString());
    
    File certfile = new File(certPath);
    File keyfile = new File(keyPath);

    // Create directory to store the keys.
    certfile.getParentFile().mkdirs();

    Files.write(certfile.toPath(), certContent.getBytes());
    Files.write(keyfile.toPath(), keyContent.getBytes());
    
    CertificateInfo cert = CertificateInfo.create(rootCA_UUID, customerUUID, label, certStart,
        certExpiry, keyPath, certPath);
    
    return cert.uuid;

  }

  public static String getCertPEM(UUID rootCA){
    CertificateInfo cert = CertificateInfo.get(rootCA);
    String certPEM = FileUtils.readFileToString(new File(cert.certificate));
    certPEM = Base64.getEncoder().encodeToString(certPEM.getBytes());
    return certPEM;
  }

  public static String getCertPEM(CertificateInfo cert){
    String certPEM = FileUtils.readFileToString(new File(cert.certificate));
    certPEM = Base64.getEncoder().encodeToString(certPEM.getBytes());
    return certPEM;
  }

  public static String getKeyPEM(CertificateInfo cert){
    String privateKeyPEM = FileUtils.readFileToString(new File(cert.privateKey));
    privateKeyPEM = Base64.getEncoder().encodeToString(privateKeyPEM.getBytes());
    return privateKeyPEM;
  }

  public static String getKeyPEM(UUID rootCA){
    CertificateInfo cert = CertificateInfo.get(rootCA);
    String privateKeyPEM = FileUtils.readFileToString(new File(cert.privateKey));
    privateKeyPEM = Base64.getEncoder().encodeToString(privateKeyPEM.getBytes());
    return privateKeyPEM;
  }
}
