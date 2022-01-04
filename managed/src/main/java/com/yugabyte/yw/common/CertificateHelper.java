// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.UniverseSetTlsParams;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.CertificateInfo.Type;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.StringReader;
import java.io.StringWriter;
import java.math.BigInteger;
import java.security.InvalidKeyException;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.PrivateKey;
import java.security.Security;
import java.security.SignatureException;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.interfaces.RSAPrivateKey;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Calendar;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import org.apache.commons.validator.routines.InetAddressValidator;
import org.bouncycastle.asn1.DERSequence;
import org.bouncycastle.asn1.x500.RDN;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x500.X500NameBuilder;
import org.bouncycastle.asn1.x500.style.BCStyle;
import org.bouncycastle.asn1.x509.BasicConstraints;
import org.bouncycastle.asn1.x509.Extension;
import org.bouncycastle.asn1.x509.GeneralName;
import org.bouncycastle.asn1.x509.GeneralNames;
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
import org.flywaydb.play.FileUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;

/** Helper class for Certificates */
public class CertificateHelper {

  public static final Logger LOG = LoggerFactory.getLogger(CertificateHelper.class);

  public static final String CLIENT_CERT = "yugabytedb.crt";
  public static final String CLIENT_KEY = "yugabytedb.key";
  public static final String DEFAULT_CLIENT = "yugabyte";
  public static final String CERT_PATH = "%s/certs/%s/%s";
  public static final String ROOT_CERT = "root.crt";
  public static final String SERVER_CERT = "server.crt";
  public static final String SERVER_KEY = "server.key.pem";
  public static final String CLIENT_NODE_SUFFIX = "-client";

  public static UUID createRootCA(String nodePrefix, UUID customerUUID, String storagePath) {
    try {
      // Default the cert label with node prefix.
      // If cert with the label already exists append number
      String certLabel = nodePrefix;
      CertificateInfo.Type certType = CertificateInfo.Type.SelfSigned;
      List<CertificateInfo> certificateInfoList =
          CertificateInfo.getWhereLabelStartsWith(nodePrefix, certType);
      if (!certificateInfoList.isEmpty()) {
        certificateInfoList.sort(Comparator.comparing(a -> a.label, Comparator.reverseOrder()));
        String[] labelArray = certificateInfoList.get(0).label.split("~");
        int lastCount = 0;
        try {
          lastCount = Integer.parseInt(labelArray[labelArray.length - 1]);
        } catch (NumberFormatException ignored) {
        }
        certLabel = nodePrefix + "~" + (++lastCount);
      }

      KeyPair keyPair = getKeyPairObject();
      UUID rootCA_UUID = UUID.randomUUID();
      Calendar cal = Calendar.getInstance();
      Date certStart = cal.getTime();
      cal.add(Calendar.YEAR, 4);
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
          new JcaContentSignerBuilder("SHA256withRSA").build(keyPair.getPrivate());
      X509CertificateHolder holder = certGen.build(signer);
      JcaX509CertificateConverter converter = new JcaX509CertificateConverter();
      converter.setProvider(new BouncyCastleProvider());
      X509Certificate x509 = converter.getCertificate(holder);
      String certPath =
          String.format(
              CERT_PATH + "/ca.%s", storagePath, customerUUID.toString(), rootCA_UUID, ROOT_CERT);
      writeCertFileContentToCertPath(Collections.singletonList(x509), certPath);
      String keyPath =
          String.format(CERT_PATH + "/ca.key.pem", storagePath, customerUUID, rootCA_UUID);
      writeKeyFileContentToKeyPath(keyPair.getPrivate(), keyPath);

      LOG.info(
          "Generated self signed cert label {} uuid {} of type {} for customer {} at paths {}, {}",
          certLabel,
          rootCA_UUID,
          certType,
          customerUUID,
          certPath,
          keyPath);

      CertificateInfo cert =
          CertificateInfo.create(
              rootCA_UUID,
              customerUUID,
              certLabel,
              certStart,
              certExpiry,
              keyPath,
              certPath,
              certType);

      LOG.info("Created Root CA for universe {}.", certLabel);
      return cert.uuid;
    } catch (NoSuchAlgorithmException
        | IOException
        | OperatorCreationException
        | CertificateException e) {
      LOG.error("Unable to create RootCA for universe {}", nodePrefix, e);
      return null;
    }
  }

  public static UUID createClientRootCA(String nodePrefix, UUID customerUUID, String storagePath) {
    return createRootCA(nodePrefix + CLIENT_NODE_SUFFIX, customerUUID, storagePath);
  }

  public static CertificateDetails createSignedCertificate(
      UUID rootCA,
      String storagePath,
      String username,
      Date certStart,
      Date certExpiry,
      String certFileName,
      String certKeyName) {
    LOG.info(
        "Creating signed certificate signed by root CA {} and user {} at path {}",
        rootCA,
        username,
        storagePath);
    try {
      // Add the security provider in case createSignedCertificate was never called.
      KeyPair clientKeyPair = getKeyPairObject();

      Calendar cal = Calendar.getInstance();
      if (certStart == null) {
        certStart = cal.getTime();
      }
      if (certExpiry == null) {
        cal.add(Calendar.YEAR, 4);
        certExpiry = cal.getTime();
      }

      CertificateInfo cert = CertificateInfo.get(rootCA);
      if (cert.privateKey == null) {
        throw new PlatformServiceException(BAD_REQUEST, "Keyfile cannot be null!");
      }
      // The first entry will be the certificate that needs to sign the necessary certificate.
      X509Certificate cer =
          getX509CertificateCertObject(FileUtils.readFileToString(new File(cert.certificate)))
              .get(0);
      X500Name subject = new JcaX509CertificateHolder(cer).getSubject();
      PrivateKey pk = null;
      try {
        pk = getPrivateKey(FileUtils.readFileToString(new File(cert.privateKey)));
      } catch (Exception e) {
        LOG.error(
            "Unable to create client CA for username {} using root CA {}", username, rootCA, e);
        throw new PlatformServiceException(BAD_REQUEST, "Could not create client cert.");
      }

      X500Name clientCertSubject = new X500Name(String.format("CN=%s", username));
      BigInteger clientSerial = BigInteger.valueOf(System.currentTimeMillis());
      PKCS10CertificationRequestBuilder p10Builder =
          new JcaPKCS10CertificationRequestBuilder(clientCertSubject, clientKeyPair.getPublic());
      ContentSigner csrContentSigner = new JcaContentSignerBuilder("SHA256withRSA").build(pk);
      PKCS10CertificationRequest csr = p10Builder.build(csrContentSigner);

      KeyUsage keyUsage =
          new KeyUsage(
              KeyUsage.digitalSignature
                  | KeyUsage.nonRepudiation
                  | KeyUsage.keyEncipherment
                  | KeyUsage.keyCertSign);

      X509v3CertificateBuilder clientCertBuilder =
          new X509v3CertificateBuilder(
              subject,
              clientSerial,
              certStart,
              certExpiry,
              csr.getSubject(),
              csr.getSubjectPublicKeyInfo());
      JcaX509ExtensionUtils clientCertExtUtils = new JcaX509ExtensionUtils();
      clientCertBuilder.addExtension(
          Extension.basicConstraints, true, new BasicConstraints(false).toASN1Primitive());
      clientCertBuilder.addExtension(
          Extension.authorityKeyIdentifier,
          false,
          clientCertExtUtils.createAuthorityKeyIdentifier(cer));
      clientCertBuilder.addExtension(
          Extension.subjectKeyIdentifier,
          false,
          clientCertExtUtils.createSubjectKeyIdentifier(csr.getSubjectPublicKeyInfo()));
      clientCertBuilder.addExtension(Extension.keyUsage, false, keyUsage.toASN1Primitive());

      InetAddressValidator ipAddressValidator = InetAddressValidator.getInstance();
      if (ipAddressValidator.isValid(username)) {
        List<GeneralName> altNames = new ArrayList<>();
        altNames.add(new GeneralName(GeneralName.iPAddress, username));
        GeneralNames subjectAltNames =
            GeneralNames.getInstance(new DERSequence(altNames.toArray(new GeneralName[] {})));
        clientCertBuilder.addExtension(Extension.subjectAlternativeName, false, subjectAltNames);
      }

      X509CertificateHolder clientCertHolder = clientCertBuilder.build(csrContentSigner);
      X509Certificate clientCert =
          new JcaX509CertificateConverter()
              .setProvider(new BouncyCastleProvider())
              .getCertificate(clientCertHolder);

      clientCert.verify(cer.getPublicKey(), "BC");

      JcaPEMWriter clientCertWriter = null;
      JcaPEMWriter clientKeyWriter = null;
      StringWriter certWriter = new StringWriter();
      StringWriter keyWriter = new StringWriter();
      CertificateDetails certificateDetails = new CertificateDetails();
      try {
        if (storagePath != null) {
          String clientCertPath = String.format("%s/%s", storagePath, certFileName);
          String clientKeyPath = String.format("%s/%s", storagePath, certKeyName);
          File clientCertfile = new File(clientCertPath);
          File clientKeyfile = new File(clientKeyPath);
          clientCertWriter = new JcaPEMWriter(new FileWriter(clientCertfile));
          clientKeyWriter = new JcaPEMWriter(new FileWriter(clientKeyfile));
        } else {
          clientCertWriter = new JcaPEMWriter(certWriter);
          clientKeyWriter = new JcaPEMWriter(keyWriter);
        }
        clientCertWriter.writeObject(clientCert);
        clientCertWriter.flush();
        clientKeyWriter.writeObject(clientKeyPair.getPrivate());
        clientKeyWriter.flush();
      } finally {
        if (clientCertWriter != null) {
          clientCertWriter.close();
        }
        if (clientKeyWriter != null) {
          clientKeyWriter.close();
        }
      }
      if (storagePath == null) {
        certificateDetails.crt = certWriter.toString();
        certificateDetails.key = keyWriter.toString();
      }
      LOG.info("Created Client CA for username {} signed by root CA {}.", username, rootCA);
      return certificateDetails;

    } catch (NoSuchAlgorithmException
        | IOException
        | OperatorCreationException
        | CertificateException
        | InvalidKeyException
        | NoSuchProviderException
        | SignatureException e) {
      LOG.error("Unable to create client CA for username {} using root CA {}", username, rootCA, e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Could not create client cert.");
    }
  }

  public static CertificateDetails createClientCertificate(
      UUID rootCA, String storagePath, String username, Date certStart, Date certExpiry) {
    return createSignedCertificate(
        rootCA, storagePath, username, certStart, certExpiry, CLIENT_CERT, CLIENT_KEY);
  }

  public static CertificateDetails createServerCertificate(
      UUID rootCA,
      String storagePath,
      String username,
      Date certStart,
      Date certExpiry,
      String certFileName,
      String certKeyName) {
    return createSignedCertificate(
        rootCA, storagePath, username, certStart, certExpiry, certFileName, certKeyName);
  }

  public static UUID uploadRootCA(
      String label,
      UUID customerUUID,
      String storagePath,
      String certContent,
      String keyContent,
      Date certStart,
      Date certExpiry,
      CertificateInfo.Type certType,
      CertificateParams.CustomCertInfo customCertInfo,
      CertificateParams.CustomServerCertData customServerCertData) {
    LOG.debug("uploadRootCA: Label: {}, customerUUID: {}", label, customerUUID.toString());
    try {
      if (certContent == null) {
        throw new PlatformServiceException(BAD_REQUEST, "Certfile can't be null");
      }
      UUID rootCA_UUID = UUID.randomUUID();
      String keyPath = null;
      CertificateInfo.CustomServerCertInfo customServerCertInfo = null;
      List<X509Certificate> x509CACerts = getX509CertificateCertObject(certContent);

      // Set certStart and certExpiry from X509Certificate if provided null
      if (certType == Type.CustomCertHostPath) {
        if (certStart == null || certStart.getTime() == 0) {
          long certStartTimestamp = 0L;
          for (X509Certificate cert : x509CACerts) {
            if (cert.getNotBefore().getTime() > certStartTimestamp) {
              certStartTimestamp = cert.getNotBefore().getTime();
            }
          }
          certStart = new Date(certStartTimestamp);
        }
        if (certExpiry == null || certExpiry.getTime() == 0) {
          long certExpiryTimestamp = Long.MAX_VALUE;
          for (X509Certificate cert : x509CACerts) {
            if (cert.getNotAfter().getTime() < certExpiryTimestamp) {
              certExpiryTimestamp = cert.getNotAfter().getTime();
            }
          }
          certExpiry = new Date(certExpiryTimestamp);
        }
      }

      // Verify the uploaded cert is a verified cert chain.
      verifyCertValidity(x509CACerts);
      if (certType == CertificateInfo.Type.SelfSigned) {
        // The first entry in the file should be the cert we want to use for generating server
        // certs.
        verifyCertSignatureAndOrder(x509CACerts, keyContent);
        keyPath =
            String.format(
                "%s/certs/%s/%s/ca.key.pem",
                storagePath, customerUUID.toString(), rootCA_UUID.toString());
      }
      if (certType == CertificateInfo.Type.CustomServerCert) {
        // Verify the upload Server Cert is a verified cert chain.
        List<X509Certificate> x509ServerCertificates =
            getX509CertificateCertObject(customServerCertData.serverCertContent);
        // Verify that the uploaded server cert was signed by the uploaded CA cert
        List<X509Certificate> combinedArrayList = new ArrayList<>(x509ServerCertificates);
        combinedArrayList.addAll(x509CACerts);
        verifyCertValidity(combinedArrayList);
        // The first entry in the file should be the cert we want to use for generating server
        // certs.
        verifyCertSignatureAndOrder(x509ServerCertificates, customServerCertData.serverKeyContent);
        String serverCertPath =
            String.format(
                "%s/certs/%s/%s/%s",
                storagePath, customerUUID.toString(), rootCA_UUID.toString(), SERVER_CERT);
        String serverKeyPath =
            String.format(
                "%s/certs/%s/%s/%s",
                storagePath, customerUUID.toString(), rootCA_UUID.toString(), SERVER_KEY);
        writeCertFileContentToCertPath(x509ServerCertificates, serverCertPath);
        writeKeyFileContentToKeyPath(
            getPrivateKey(customServerCertData.serverKeyContent), serverKeyPath);
        customServerCertInfo =
            new CertificateInfo.CustomServerCertInfo(serverCertPath, serverKeyPath);
      }
      String certPath =
          String.format(
              "%s/certs/%s/%s/ca.%s",
              storagePath, customerUUID.toString(), rootCA_UUID.toString(), ROOT_CERT);

      writeCertFileContentToCertPath(getX509CertificateCertObject(certContent), certPath);

      CertificateInfo cert;
      switch (certType) {
        case SelfSigned:
          {
            writeKeyFileContentToKeyPath(getPrivateKey(keyContent), keyPath);
            cert =
                CertificateInfo.create(
                    rootCA_UUID,
                    customerUUID,
                    label,
                    certStart,
                    certExpiry,
                    keyPath,
                    certPath,
                    certType);
            break;
          }
        case CustomCertHostPath:
          {
            cert =
                CertificateInfo.create(
                    rootCA_UUID,
                    customerUUID,
                    label,
                    certStart,
                    certExpiry,
                    certPath,
                    customCertInfo);
            break;
          }
        case CustomServerCert:
          {
            cert =
                CertificateInfo.create(
                    rootCA_UUID,
                    customerUUID,
                    label,
                    certStart,
                    certExpiry,
                    certPath,
                    customServerCertInfo);
            break;
          }
        default:
          {
            throw new PlatformServiceException(BAD_REQUEST, "certType should be valid.");
          }
      }
      LOG.info(
          "Uploaded cert label {} (uuid {}) of type {} at paths"
              + " '{}', '{}' with custom cert info {}",
          label,
          rootCA_UUID,
          certType,
          certPath,
          String.valueOf(keyPath),
          Json.toJson(customCertInfo));
      return cert.uuid;
    } catch (IOException | NoSuchAlgorithmException e) {
      LOG.error(
          "uploadRootCA: Could not generate checksum for cert {} for customer {}",
          label,
          customerUUID.toString());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "uploadRootCA: Checksum generation failed.");
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
    return (cer1.getCustomCertInfo().nodeCertPath.equals(cer2.getCustomCertInfo().nodeCertPath)
        || cer1.getCustomCertInfo().nodeKeyPath.equals(cer2.getCustomCertInfo().nodeKeyPath));
  }

  public static void createChecksums() {
    List<CertificateInfo> certs = CertificateInfo.getAllNoChecksum();
    for (CertificateInfo cert : certs) {
      try {
        cert.setChecksum();
      } catch (IOException | NoSuchAlgorithmException e) {
        // Log error, but don't cause it to error out.
        LOG.error("Could not generate checksum for cert: {}", cert.certificate);
      }
    }
  }

  @SuppressWarnings("unchecked")
  public static List<X509Certificate> getX509CertificateCertObject(String certContent) {
    try {
      InputStream in = null;
      byte[] certEntryBytes = certContent.getBytes();
      in = new ByteArrayInputStream(certEntryBytes);
      CertificateFactory certFactory = CertificateFactory.getInstance("X.509");
      return (List<X509Certificate>) (List<?>) certFactory.generateCertificates(in);
    } catch (CertificateException e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Unable to get cert Object");
    }
  }

  public static PrivateKey getPrivateKey(String keyContent) {
    try (PemReader pemReader = new PemReader(new StringReader(new String(keyContent)))) {
      PemObject pemObject = pemReader.readPemObject();
      byte[] bytes = pemObject.getContent();
      PKCS8EncodedKeySpec spec = new PKCS8EncodedKeySpec(bytes);
      KeyFactory kf = KeyFactory.getInstance("RSA");
      return kf.generatePrivate(spec);
    } catch (Exception e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Unable to get Private Key");
    }
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

  public static void writeKeyFileContentToKeyPath(PrivateKey keyContent, String keyPath) {
    File keyFile = new File(keyPath);
    try (JcaPEMWriter keyWriter = new JcaPEMWriter(new FileWriter(keyFile))) {
      keyWriter.writeObject(keyContent);
      keyWriter.flush();
    } catch (Exception e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Save privateKey failed.");
    }
  }

  public static void writeCertFileContentToCertPath(List<X509Certificate> certs, String certPath) {
    File certfile = new File(certPath);
    // Create directory to store the certFile.
    certfile.getParentFile().mkdirs();
    try (JcaPEMWriter certWriter = new JcaPEMWriter(new FileWriter(certfile))) {
      for (X509Certificate cert : certs) {
        certWriter.writeObject(cert);
        certWriter.flush();
      }
    } catch (Exception e) {
      LOG.error(e.getMessage());
      throw new RuntimeException("Save certContent failed.");
    }
  }

  public static KeyPair getKeyPairObject() {
    try {
      // Add the security provider in case it was never called.
      Security.addProvider(new BouncyCastleProvider());
      KeyPairGenerator keypairGen = KeyPairGenerator.getInstance("RSA");
      keypairGen.initialize(2048);
      return keypairGen.generateKeyPair();
    } catch (Exception e) {
      LOG.error(e.getMessage());
    }
    return null;
  }

  private static boolean verifySignature(X509Certificate cert, String key) {
    try {
      // Add the security provider in case verifySignature was never called.
      getKeyPairObject();
      RSAPrivateKey privKey = (RSAPrivateKey) getPrivateKey(key);
      RSAPublicKey publicKey = (RSAPublicKey) cert.getPublicKey();
      return privKey.getModulus().toString().equals(publicKey.getModulus().toString());
    } catch (Exception e) {
      LOG.error("Cert or key is invalid." + e.getMessage());
    }
    return false;
  }

  // Verify that each certificate in the root chain has been signed by
  // another cert present in the uploaded file.
  private static void verifyCertValidity(List<X509Certificate> certs) {
    certs
        .stream()
        .forEach(
            cert -> {
              if (!certs
                  .stream()
                  .anyMatch(potentialRootCert -> verifyCertValidity(cert, potentialRootCert))) {
                X500Name x500Name = new X500Name(cert.getSubjectX500Principal().getName());
                RDN cn = x500Name.getRDNs(BCStyle.CN)[0];
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    "Certificate with CN = "
                        + cn.getFirst().getValue()
                        + " has no associated root");
              }
              verifyCertDateValidity(cert);
            });
  }

  // Verify that certificate is currently valid and valid for 1 day
  private static void verifyCertDateValidity(X509Certificate cert) {
    Calendar cal = Calendar.getInstance();
    cal.add(Calendar.DATE, 1);
    Date oneDayAfterToday = cal.getTime();
    try {
      cert.checkValidity();
      cert.checkValidity(oneDayAfterToday);
    } catch (Exception e) {
      X500Name x500Name = new X500Name(cert.getSubjectX500Principal().getName());
      RDN cn = x500Name.getRDNs(BCStyle.CN)[0];
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Certificate with CN = " + cn.getFirst().getValue() + " has invalid start/end dates.");
    }
  }

  private static boolean verifyCertValidity(
      X509Certificate cert, X509Certificate potentialRootCert) {
    try {
      cert.verify(potentialRootCert.getPublicKey());
      return true;
    } catch (Exception exp) {
      // Exception means the verify failed.
      return false;
    }
  }

  private static boolean verifyCertSignatureAndOrder(
      List<X509Certificate> x509Certificates, String keyContent) {
    if (!verifySignature(x509Certificates.get(0), keyContent)) {
      // If the first certificate is not the right one, maybe the user has entered the
      // certificates in the wrong order. Check and update the customer with the right
      // message.
      x509Certificates
          .stream()
          .forEach(
              x509Certificate -> {
                if (verifySignature(x509Certificate, keyContent)) {
                  X500Name x500Name =
                      new X500Name(x509Certificate.getSubjectX500Principal().getName());
                  RDN cn = x500Name.getRDNs(BCStyle.CN)[0];
                  throw new PlatformServiceException(
                      BAD_REQUEST,
                      "Certificate with CN = "
                          + cn.getFirst().getValue()
                          + "should be the first entry in the file.");
                }
              });
      throw new PlatformServiceException(BAD_REQUEST, "Certificate and key don't match.");
    }
    return true;
  }
}
