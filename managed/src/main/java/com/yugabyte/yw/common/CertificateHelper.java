// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.yugabyte.yw.forms.CertificateParams;
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
import org.bouncycastle.cert.jcajce.JcaX509CertificateHolder;
import org.bouncycastle.cert.jcajce.JcaX509ExtensionUtils;
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder;
import org.bouncycastle.jce.provider.BouncyCastleProvider;
import org.bouncycastle.openssl.jcajce.JcaPEMWriter;
import org.bouncycastle.operator.ContentSigner;
import org.bouncycastle.operator.OperatorCreationException;
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder;
import org.bouncycastle.pkcs.PKCS10CertificationRequest;
import org.bouncycastle.pkcs.PKCS10CertificationRequestBuilder;
import org.bouncycastle.pkcs.jcajce.JcaPKCS10CertificationRequestBuilder;
import org.bouncycastle.util.io.pem.PemObject;
import org.bouncycastle.util.io.pem.PemReader;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import org.flywaydb.play.FileUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.libs.Json;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.StringWriter;
import java.io.StringReader;
import java.io.Writer;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.math.BigInteger;
import java.security.InvalidKeyException;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.PrivateKey;
import java.security.interfaces.RSAPrivateKey;
import java.security.interfaces.RSAPublicKey;
import java.security.Security;
import java.security.SignatureException;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.Arrays;
import java.util.Base64;
import java.util.Calendar;
import java.util.Date;
import java.util.List;
import java.util.UUID;

/**
 * Helper class for Certificates
 */

public class CertificateHelper {

  public static final Logger LOG = LoggerFactory.getLogger(CertificateHelper.class);

  public static final String CLIENT_CERT = "yugabytedb.crt";
  public static final String CLIENT_KEY = "yugabytedb.key";
  public static final String PLATFORM_CERT = "platform.crt";
  public static final String PLATFORM_KEY = "platform.key";
  public static final String DEFAULT_CLIENT = "yugabyte";
  public static final String CERT_PATH = "%s/certs/%s/%s";
  public static final String ROOT_CERT = "root.crt";

  public static UUID createRootCA(String nodePrefix, UUID customerUUID, String storagePath) {
    try {
      KeyPair keyPair = getKeyPairObject();

      UUID rootCA_UUID = UUID.randomUUID();
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
          subject, serial, certStart, certExpiry, subject, keyPair.getPublic());
      BasicConstraints basicConstraints = new BasicConstraints(1);
      KeyUsage keyUsage = new KeyUsage(KeyUsage.digitalSignature | KeyUsage.nonRepudiation |
          KeyUsage.keyEncipherment | KeyUsage.keyCertSign);
      certGen.addExtension(Extension.basicConstraints, true, basicConstraints.toASN1Primitive());
      certGen.addExtension(Extension.keyUsage, true, keyUsage.toASN1Primitive());

      ContentSigner signer = new JcaContentSignerBuilder("SHA256withRSA")
          .build(keyPair.getPrivate());
      X509CertificateHolder holder = certGen.build(signer);
      JcaX509CertificateConverter converter = new JcaX509CertificateConverter();
      converter.setProvider(new BouncyCastleProvider());
      X509Certificate x509 = converter.getCertificate(holder);

      String certPath = String.format(CERT_PATH + "/ca.%s", storagePath,
          customerUUID.toString(), rootCA_UUID.toString(), ROOT_CERT);
      File certfile = new File(certPath);
      // Create directory to store the certFile.
      certfile.getParentFile().mkdirs();
      writeCertFileContentToCertPath(x509, new FileWriter(certfile));

      String keyPath = String.format(CERT_PATH + "/ca.key.pem", storagePath,
          customerUUID.toString(), rootCA_UUID.toString());
      File keyFile = new File(keyPath);
      writeKeyFileContentToKeyPath(keyPair.getPrivate(), new FileWriter(keyFile));

      LOG.info(
        "Generated self signed cert label {} uuid {} for customer {} at paths {}, {}.",
        nodePrefix, rootCA_UUID, customerUUID,
        certPath, keyPath
      );

      CertificateInfo cert = CertificateInfo.create(
        rootCA_UUID, customerUUID, nodePrefix,
        certStart, certExpiry, certPath, keyPath, null, null
      );

      // Create the client files for psql connectivity as well as for
      // mutual TLS between platform and DB nodes.
      createPlatformCertificate(rootCA_UUID, CertificateHelper.DEFAULT_CLIENT, null, null,
          true /* if cert files need to be saved or just returned. */);
      createClientCertificate(rootCA_UUID, CertificateHelper.DEFAULT_CLIENT, null, null,
          true /* if cert files need to be saved or just returned. */);
      cert.setPlatformCert(getPlatformCertFile(rootCA_UUID));
      cert.setPlatformKey(getPlatformKeyFile(rootCA_UUID));

      LOG.info("Created Root CA for {}.", nodePrefix);
      return cert.uuid;
    } catch (NoSuchAlgorithmException | IOException | OperatorCreationException |
      CertificateException e) {
      LOG.error("Unable to create RootCA for universe " + nodePrefix, e);
      return null;
    }
  }

  public static JsonNode createPlatformCertificate(UUID rootCA, String username,
                                                   Date certStart, Date certExpiry,
                                                   boolean writeToFile) {
    return createCertificate(rootCA, username, certStart, certExpiry, writeToFile, true);
  }

  public static JsonNode createClientCertificate(UUID rootCA, String username,
                                                 Date certStart, Date certExpiry,
                                                 boolean writeToFile) {
    return createCertificate(rootCA, username, certStart, certExpiry, writeToFile, false);
  }


  public static JsonNode createCertificate(UUID rootCA, String username,
                                           Date certStart, Date certExpiry,
                                           boolean writeToFile, boolean isPlatformCert) {
    LOG.info("Creating client certificate signed by root CA {} and user {}.",
      rootCA, username);
    try {
      // Add the security provider in case createClientCertificate was never called.
      KeyPair clientKeyPair = getKeyPairObject();

      Calendar cal = Calendar.getInstance();
      if (certStart == null) {
        certStart = cal.getTime();
      }
      if (certExpiry == null) {
        cal.add(Calendar.YEAR, 1);
        certExpiry = cal.getTime();
      }

      CertificateInfo cert = CertificateInfo.get(rootCA);
      if (cert.privateKey == null) {
        throw new RuntimeException("Keyfile cannot be null!");
      }
      X509Certificate cer = getX509CertificateCertObject(FileUtils.readFileToString
          (new File(cert.certificate)));
      X500Name subject = new JcaX509CertificateHolder(cer).getSubject();
      PrivateKey pk = null;
      try {
        pk = getPrivateKey(FileUtils.readFileToString(new File(cert.privateKey)));
      } catch (Exception e) {
        LOG.error("Unable to create client CA for username {} using root CA {}.",
            username, rootCA, e);
        throw new RuntimeException("Could not create client cert.");
      }

      X500Name clientCertSubject = new X500Name(String.format("CN=%s", username));
      BigInteger clientSerial = BigInteger.valueOf(System.currentTimeMillis());
      PKCS10CertificationRequestBuilder p10Builder = new JcaPKCS10CertificationRequestBuilder(
          clientCertSubject,
          clientKeyPair.getPublic());
      ContentSigner csrContentSigner = new JcaContentSignerBuilder("SHA256withRSA")
          .build(pk);
      PKCS10CertificationRequest csr = p10Builder.build(csrContentSigner);

      KeyUsage keyUsage = new KeyUsage(KeyUsage.digitalSignature | KeyUsage.nonRepudiation |
          KeyUsage.keyEncipherment | KeyUsage.keyCertSign);

      X509v3CertificateBuilder clientCertBuilder = new X509v3CertificateBuilder(
          subject, clientSerial, certStart, certExpiry,
          csr.getSubject(), csr.getSubjectPublicKeyInfo());
      JcaX509ExtensionUtils clientCertExtUtils = new JcaX509ExtensionUtils();
      clientCertBuilder.addExtension(Extension.basicConstraints, true,
          new BasicConstraints(false).toASN1Primitive());
      clientCertBuilder.addExtension(Extension.authorityKeyIdentifier, false,
          clientCertExtUtils.createAuthorityKeyIdentifier(cer));
      clientCertBuilder.addExtension(Extension.subjectKeyIdentifier, false,
          clientCertExtUtils.createSubjectKeyIdentifier(csr.getSubjectPublicKeyInfo()));
      clientCertBuilder.addExtension(Extension.keyUsage, false,
          keyUsage.toASN1Primitive());

      X509CertificateHolder clientCertHolder = clientCertBuilder.build(csrContentSigner);
      X509Certificate clientCert = new JcaX509CertificateConverter()
          .setProvider(new BouncyCastleProvider())
          .getCertificate(clientCertHolder);

      clientCert.verify(cer.getPublicKey(), "BC");

      StringWriter certWriter = new StringWriter();
      StringWriter keyWriter = new StringWriter();
      ObjectNode bodyJson = Json.newObject();
      if (writeToFile) {
        String certPath = isPlatformCert ?
            getPlatformCertFile(cert.uuid) : getClientCertFile(cert.uuid);
        String keyPath = isPlatformCert ?
            getPlatformKeyFile(cert.uuid) : getClientKeyFile(cert.uuid);
        File certFile = new File(certPath);
        File keyFile = new File(keyPath);
        writeCertFileContentToCertPath(clientCert, new FileWriter(certFile));
        writeKeyFileContentToKeyPath(clientKeyPair.getPrivate(), new FileWriter(keyFile));
      } else {
        writeCertFileContentToCertPath(clientCert, certWriter);
        writeKeyFileContentToKeyPath(clientKeyPair.getPrivate(), keyWriter);
      }
      if (!writeToFile) {
        bodyJson.put(isPlatformCert ? PLATFORM_CERT : CLIENT_CERT, certWriter.toString());
        bodyJson.put(isPlatformCert ? PLATFORM_KEY : CLIENT_KEY, keyWriter.toString());
      }
      LOG.info("Created Client CA for username {} signed by root CA {}.", username, rootCA);
      return bodyJson;

    } catch (NoSuchAlgorithmException | IOException | OperatorCreationException |
        CertificateException | InvalidKeyException | NoSuchProviderException |
        SignatureException e) {
      LOG.error("Unable to create client CA for username {} using root CA {}", username, rootCA, e);
      throw new RuntimeException("Could not create client cert.");
    }
  }

  public static UUID uploadRootCA(
    String label, UUID customerUUID, String storagePath,
    String certContent, String keyContent, Date certStart,
    Date certExpiry, CertificateInfo.Type certType,
    CertificateParams.CustomCertInfo customCertInfo) throws IOException {

    if (certContent == null) {
      throw new RuntimeException("Certfile can't be null.");
    }
    UUID rootCA_UUID = UUID.randomUUID();
    String keyPath = null;

    X509Certificate x509Certificate = getX509CertificateCertObject(certContent);
    if (certType == CertificateInfo.Type.SelfSigned) {
      PrivateKey privateKey = getPrivateKey(keyContent);
      if (!verifySignature(x509Certificate, privateKey)) {
        throw new RuntimeException("Invalid certificate.");
      }
      keyPath = String.format("%s/certs/%s/%s/ca.key.pem", storagePath,
          customerUUID.toString(), rootCA_UUID.toString());
      File keyFile = new File(keyPath);
      writeKeyFileContentToKeyPath(privateKey, new FileWriter(keyFile));
    } else {
      if (!isValidCACert(x509Certificate))
        throw new RuntimeException("Invalid CA certificate.");
    }
    String certPath = String.format("%s/certs/%s/%s/ca.%s", storagePath,
        customerUUID.toString(), rootCA_UUID.toString(), ROOT_CERT);
    File certfile = new File(certPath);
    // Create directory to store the certFile.
    certfile.getParentFile().mkdirs();
    writeCertFileContentToCertPath(x509Certificate, new FileWriter(certfile));

    LOG.info(
        "Uploaded cert label {} (uuid {}) of type {} at paths {}, {}.",
        label, rootCA_UUID, certType,
        certPath, ((keyPath == null) ? "no private key" : keyPath)
    );
    CertificateInfo cert = null;
    try {
      if (certType == CertificateInfo.Type.SelfSigned) {
        // Generate a platform client cert/key pair.
        cert = CertificateInfo.create(rootCA_UUID, customerUUID, label, certStart,
            certExpiry, certPath, keyPath, null, null);
        // Create the client files for psql connectivity as well as for
        // mutual TLS between platform and DB nodes.
        createClientCertificate(rootCA_UUID, CertificateHelper.DEFAULT_CLIENT, null, null,
            true /* if cert files need to be saved or just returned. */);
        createPlatformCertificate(rootCA_UUID, CertificateHelper.DEFAULT_CLIENT, null, null,
            true /* if cert files need to be saved or just returned. */);
        cert.setPlatformCert(getPlatformCertFile(rootCA_UUID));
        cert.setPlatformKey(getPlatformKeyFile(rootCA_UUID));
      } else {
        cert = CertificateInfo.create(rootCA_UUID, customerUUID, label, certStart,
            certExpiry, certPath, customCertInfo, null, null);
        // Use the provided platform client cert/key pair.
        if (customCertInfo != null) {
          String clientCertContent = customCertInfo.platformCertContent;
          String clientKeyContent = customCertInfo.platformKeyContent;
          if (clientCertContent != null && clientKeyContent != null) {
            String clientCertfile = getPlatformCertFile(cert.uuid);
            String clientKeyfile = getPlatformKeyFile(cert.uuid);
            X509Certificate clientCert = getX509CertificateCertObject(clientCertContent);
            PrivateKey clientKey = getPrivateKey(clientKeyContent);
            if (!verifySignature(clientCert, clientKey)) {
              throw new RuntimeException("Invalid certificate.");
            }
            try {
              clientCert.verify(x509Certificate.getPublicKey(), "BC");
            } catch (Exception e) {
              throw new RuntimeException("Platform cert not signed by root cert.");
            }
            writeCertFileContentToCertPath(clientCert, new FileWriter(new File(clientCertfile)));
            writeKeyFileContentToKeyPath(clientKey, new FileWriter(new File(clientKeyfile)));
            cert.setPlatformCert(getPlatformCertFile(rootCA_UUID));
            cert.setPlatformKey(getPlatformKeyFile(rootCA_UUID));
          }
        }
      }
      return cert.uuid;
    } catch (IOException | NoSuchAlgorithmException e) {
      LOG.error("Could not generate checksum for cert.");
      throw new RuntimeException("Checksum generation failed.");
    }
  }

  public static String getCertPEMFileContents(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    String certPEM = FileUtils.readFileToString(new File(cert.certificate));
    return certPEM;
  }

  public static String getCertPEM(UUID rootCA) {
    String certPEM = getCertPEMFileContents(rootCA);
    certPEM = Base64.getEncoder().encodeToString(certPEM.getBytes());
    return certPEM;
  }

  public static String getCertPEM(CertificateInfo cert) {
    String certPEM = FileUtils.readFileToString(new File(cert.certificate));
    certPEM = Base64.getEncoder().encodeToString(certPEM.getBytes());
    return certPEM;
  }

  public static String getKeyPEM(CertificateInfo cert) {
    String privateKeyPEM = FileUtils.readFileToString(new File(cert.privateKey));
    privateKeyPEM = Base64.getEncoder().encodeToString(privateKeyPEM.getBytes());
    return privateKeyPEM;
  }

  public static String getKeyPEM(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    String privateKeyPEM = FileUtils.readFileToString(new File(cert.privateKey));
    privateKeyPEM = Base64.getEncoder().encodeToString(privateKeyPEM.getBytes());
    return privateKeyPEM;
  }

  public static String getClientCertFile(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    File certFile = new File(cert.certificate);
    String path = certFile.getParentFile().toString();
    return String.format("%s/%s", path, CLIENT_CERT);
  }

  public static String getClientKeyFile(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    File certFile = new File(cert.certificate);
    String path = certFile.getParentFile().toString();
    return String.format("%s/%s", path, CLIENT_KEY);
  }

  public static String getPlatformCertFile(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    File certFile = new File(cert.certificate);
    String path = certFile.getParentFile().toString();
    return String.format("%s/%s", path, PLATFORM_CERT);
  }

  public static String getPlatformKeyFile(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    File certFile = new File(cert.certificate);
    String path = certFile.getParentFile().toString();
    return String.format("%s/%s", path, PLATFORM_KEY);
  }

  public static boolean areCertsDiff(UUID cert1, UUID cert2) {
    try {
      CertificateInfo cer1 = CertificateInfo.get(cert1);
      CertificateInfo cer2 = CertificateInfo.get(cert2);
      FileInputStream is1 = new FileInputStream(new File(cer1.certificate));
      FileInputStream is2 = new FileInputStream(new File(cer2.certificate));
      CertificateFactory fact = CertificateFactory.getInstance("X.509");
      X509Certificate certObj1 = (X509Certificate) fact.generateCertificate(is1);
      X509Certificate certObj2 = (X509Certificate) fact.generateCertificate(is2);
      return !certObj2.equals(certObj1);
    } catch (IOException | CertificateException e) {
      LOG.error("Unable to read certs {}: {}", cert1.toString(), cert2.toString());
      throw new RuntimeException("Could not read certs to compare. " + e);
    }
  }

  public static boolean arePathsSame(UUID cert1, UUID cert2) {
    CertificateInfo cer1 = CertificateInfo.get(cert1);
    CertificateInfo cer2 = CertificateInfo.get(cert2);
    return (cer1.getCustomCertInfo().nodeCertPath.equals(cer2.getCustomCertInfo().nodeCertPath) ||
        cer1.getCustomCertInfo().nodeKeyPath.equals(cer2.getCustomCertInfo().nodeKeyPath));
  }

  public static void createChecksums() {
    List<CertificateInfo> certs = CertificateInfo.getAllNoChecksum();
    for (CertificateInfo cert : certs) {
      try {
        cert.setChecksum();
      } catch (IOException | NoSuchAlgorithmException e) {
        // Log error, but don't cause it to error out.
        LOG.error("Could not generate checksum for cert: {}.", cert.certificate);
      }
    }
  }

  public static X509Certificate getX509CertificateCertObject(String certContent) {
    try {
      InputStream in = null;
      byte[] certEntryBytes = certContent.getBytes();
      in = new ByteArrayInputStream(certEntryBytes);
      CertificateFactory certFactory = CertificateFactory.getInstance("X.509");
      return (X509Certificate) certFactory.generateCertificate(in);
    } catch (CertificateException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Unable to get cert Object.");
    }
  }

  public static PrivateKey getPrivateKey(String keyContent) {
    Security.addProvider(new BouncyCastleProvider());
    try (PemReader pemReader = new PemReader(new StringReader(new String(keyContent)))) {
      PemObject pemObject = pemReader.readPemObject();
      byte[] bytes = pemObject.getContent();
      PKCS8EncodedKeySpec spec = new PKCS8EncodedKeySpec(bytes);
      KeyFactory kf = KeyFactory.getInstance("RSA");
      return kf.generatePrivate(spec);
    } catch (Exception e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Unable to get Private Key.");
    }
  }

  public static void writeKeyFileContentToKeyPath(PrivateKey keyContent, Writer writer) {
    try (JcaPEMWriter keyWriter = new JcaPEMWriter(writer)) {
      keyWriter.writeObject(keyContent);
      keyWriter.flush();
    } catch (Exception e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Save privateKey failed.");
    }
  }

  public static void writeCertFileContentToCertPath(X509Certificate cert, Writer writer) {
    try (JcaPEMWriter certWriter = new JcaPEMWriter(writer)) {
      certWriter.writeObject(cert);
      certWriter.flush();
    } catch (Exception e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Save certContent failed.");
    }
  }

  public static KeyPair getKeyPairObject() {
    KeyPairGenerator keypairGen = null;
    try {
      // Add the security provider in case it was never called.
      Security.addProvider(new BouncyCastleProvider());
      keypairGen = KeyPairGenerator.getInstance("RSA");
      keypairGen.initialize(2048);
    } catch (Exception e) {
      LOG.error(e.getMessage());
    }
    return keypairGen.generateKeyPair();
  }

  private static boolean verifySignature(X509Certificate cert, PrivateKey key) {
    try {
      // Add the security provider in case verifySignature was never called.
      getKeyPairObject();
      RSAPrivateKey privKey = (RSAPrivateKey) key;
      RSAPublicKey publicKey = (RSAPublicKey) cert.getPublicKey();
      return privKey.getModulus().toString().equals(publicKey.getModulus().toString());
    } catch (Exception e) {
      LOG.error("Cert or key is invalid." + e.getMessage());
    }
    return false;
  }

  private static boolean isValidCACert(X509Certificate cert) {
    try {
      cert.verify(cert.getPublicKey());
      return true;
    } catch (Exception exp) {
      LOG.error(exp.getMessage());
      return false;
    }
  }
}
