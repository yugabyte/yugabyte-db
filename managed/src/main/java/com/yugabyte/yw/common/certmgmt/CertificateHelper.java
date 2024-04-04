// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.certmgmt;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.cronutils.utils.StringUtils;
import com.google.common.base.Strings;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.providers.CertificateProviderInterface;
import com.yugabyte.yw.common.certmgmt.providers.CertificateSelfSigned;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.CertificateParams;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.FileData;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.Universe;
import io.ebean.annotation.EnumValue;
import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStream;
import java.io.StringReader;
import java.io.StringWriter;
import java.math.BigInteger;
import java.security.Key;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.PrivateKey;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.interfaces.RSAPrivateKey;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.PKCS8EncodedKeySpec;
import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Calendar;
import java.util.Collection;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.apache.commons.lang3.tuple.Pair;
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
import org.bouncycastle.cert.jcajce.JcaX509ExtensionUtils;
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder;
import org.bouncycastle.jce.provider.BouncyCastleProvider;
import org.bouncycastle.openssl.jcajce.JcaPEMWriter;
import org.bouncycastle.operator.ContentSigner;
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder;
import org.bouncycastle.pkcs.PKCS10CertificationRequest;
import org.bouncycastle.pkcs.PKCS10CertificationRequestBuilder;
import org.bouncycastle.pkcs.jcajce.JcaPKCS10CertificationRequestBuilder;
import org.bouncycastle.util.io.pem.PemObject;
import org.bouncycastle.util.io.pem.PemReader;
import org.flywaydb.play.FileUtils;
import play.libs.Json;

@Slf4j
/* Helper class for Certificates */
public class CertificateHelper {
  public static final Pattern CERT_LABEL_PATTERN = Pattern.compile("(.*)~([0-9]+)(.*)");

  public static final String CERT_PATH = "%s/certs/%s/%s";

  public static final String ROOT_CERT = "root.crt";

  public static final String SERVER_CERT = "server.crt";
  public static final String SERVER_KEY = "server.key.pem";

  public static final String CLIENT_NODE_SUFFIX = "-client";
  public static final String DEFAULT_CLIENT = "yugabyte";

  public static final String CLIENT_CERT = "yugabytedb.crt";
  public static final String CLIENT_KEY = "yugabytedb.key";

  public static final String SIGNATURE_ALGO = "SHA256withRSA";

  private static final String CERTS_NODE_SUBDIR = "/yugabyte-tls-config";
  private static final String CERT_CLIENT_NODE_SUBDIR = "/yugabyte-client-tls-config";

  private final RuntimeConfGetter runtimeConfGetter;

  // We are only interested in n2n certs as that is the only case where master/tserver communication
  // breaks. In other scenarios we will be able to rotate expired certs.
  private static final List<String> certsListForValidityCheck =
      ImmutableList.of("Node To Node Cert Expiry Days");

  @Inject
  public CertificateHelper(RuntimeConfGetter runtimeConfGetter) {
    this.runtimeConfGetter = runtimeConfGetter;
  }

  public enum CertificateType {
    @EnumValue("ROOT_CA_CERT")
    ROOT_CA_CERT,

    @EnumValue("CLIENT_CA_CERT")
    CLIENT_CA_CERT,

    @EnumValue("USER_NODE_CERT")
    USER_NODE_CERT,

    @EnumValue("USER_CLIENT_CERT")
    USER_CLIENT_CERT
  }

  public static String getClientCertPath(Config config, UUID customerUUID, UUID clientRootCA) {
    return String.format(
        CertificateHelper.CERT_PATH,
        config.getString(AppConfigHelper.YB_STORAGE_PATH),
        customerUUID.toString(),
        clientRootCA.toString());
  }

  public static String getCACertPath(String storagePath, UUID customerUUID, UUID caCertUUID) {
    return String.format(
        CERT_PATH + "/ca.%s",
        storagePath,
        customerUUID.toString(),
        caCertUUID.toString(),
        ROOT_CERT);
  }

  public static String getCAKeyPath(String storagePath, UUID customerUUID, UUID caCertUUID) {
    return String.format(
        CERT_PATH + "/ca.key.pem", storagePath, customerUUID.toString(), caCertUUID.toString());
  }

  public static String getCADirPath(String storagePath, UUID customerUUID, UUID caCertUUID) {
    return String.format(CERT_PATH, storagePath, customerUUID.toString(), caCertUUID.toString());
  }

  private static String generateUniqueRootCALabel(String nodePrefix, CertConfigType certType) {
    // Default the cert label is same as the node prefix.
    // If cert with the label already exists, the next number starting from 1 is appended.
    List<CertificateInfo> certificateInfoList =
        CertificateInfo.getWhereLabelStartsWith(nodePrefix, certType);
    int nextLabelNumber = certificateInfoList.size() > 0 ? 1 : 0;
    for (CertificateInfo cInfo : certificateInfoList) {
      Matcher matcher = CERT_LABEL_PATTERN.matcher(cInfo.getLabel());
      if (matcher.find()) {
        String number = matcher.group(2);
        try {
          nextLabelNumber = Math.max(Integer.parseInt(number) + 1, nextLabelNumber);
        } catch (NumberFormatException ignored) {
          log.error("Unable to parse {}", number);
        }
      }
    }
    String certLabel = nextLabelNumber == 0 ? nodePrefix : (nodePrefix + "~" + nextLabelNumber);
    log.debug("New generated cert label - {}", certLabel);
    return certLabel;
  }

  public UUID createRootCA(Config config, String nodePrefix, UUID customerUUID) {
    log.info("Creating root certificate for {}", nodePrefix);

    try {
      String storagePath = config.getString(AppConfigHelper.YB_STORAGE_PATH);
      CertConfigType certType = CertConfigType.SelfSigned;
      String certLabel = generateUniqueRootCALabel(nodePrefix, certType);

      UUID rootCA_UUID = UUID.randomUUID();
      KeyPair keyPair = getKeyPairObject();

      CertificateSelfSigned obj = new CertificateSelfSigned(rootCA_UUID, config, this);
      X509Certificate x509 = obj.generateCACertificate(certLabel, keyPair);
      Pair<String, String> location = obj.dumpCACertBundle(storagePath, customerUUID);
      Date certStart = x509.getNotBefore();
      Date certExpiry = x509.getNotAfter();

      log.info(
          "Generated self signed cert label {} uuid {} of type {} for customer {} at paths {}, {}",
          certLabel,
          rootCA_UUID,
          certType,
          customerUUID,
          location.getLeft(),
          location.getRight());

      CertificateInfo cert =
          CertificateInfo.create(
              rootCA_UUID,
              customerUUID,
              certLabel,
              certStart,
              certExpiry,
              location.getRight(),
              location.getLeft(),
              certType);
      log.info("Created Root CA for universe {}.", certLabel);
      return cert.getUuid();
    } catch (Exception e) {
      log.error(String.format("Unable to create RootCA for universe %s", nodePrefix), e);
      return null;
    }
  }

  public UUID createClientRootCA(Config config, String nodePrefix, UUID customerUUID) {
    log.info("Creating a client root certificate for {}", nodePrefix);
    return createRootCA(config, nodePrefix + CLIENT_NODE_SUFFIX, customerUUID);
  }

  public static CertificateDetails dumpNewCertsToFile(
      String storagePath,
      String certFileName,
      String certKeyName,
      X509Certificate clientCert,
      PrivateKey pKey,
      boolean syncCertsToDB)
      throws IOException {
    CertificateDetails certificateDetails = new CertificateDetails();

    if (storagePath != null) {
      // get file path write it there
      String clientCertPath = String.format("%s/%s", storagePath, certFileName);
      String clientKeyPath = String.format("%s/%s", storagePath, certKeyName);

      /**
       * We generate certs for two scenarios. 1. Node<->Node Encryption 2. Client<->Node Encryption
       * For the first case, we generate the certs which are specific to node & are copied over to
       * the node(platform doesn't store the same). For the second case, platform generate those
       * certs with DEFAULT_CLIENT, i.e, yugabyte, that we need to store corresponding to that
       * certificate. Therefore, we are storing only the later certs in the DB.
       */
      writeCertFileContentToCertPath(clientCert, clientCertPath, syncCertsToDB);
      writeKeyFileContentToKeyPath(pKey, clientKeyPath, syncCertsToDB);
      log.info(
          "Dumping certificate {} at Path {}",
          clientCert.getSubjectDN().toString(),
          clientCertPath);
    } else {
      // storagePath is null, converting it to string and returning it.
      certificateDetails.crt = getAsPemString(clientCert);
      certificateDetails.key = getAsPemString(pKey);
      log.info("Returning certificate {} as Strings", clientCert.getSubjectDN().toString());
    }

    return certificateDetails;
  }

  public static CertificateDetails createClientCertificate(
      Config config, UUID customerUUID, UUID rootCaUUID) {
    return createClientCertificate(
        config,
        rootCaUUID,
        getClientCertPath(config, customerUUID, rootCaUUID),
        DEFAULT_CLIENT,
        null,
        null);
  }

  public static CertificateDetails createClientCertificate(
      Config config,
      UUID rootCA,
      String storagePath,
      String username,
      Date certStart,
      Date certExpiry) {
    log.info("Creating client certificate for {}", username);

    CertificateInfo rootCertConfigInfo = CertificateInfo.get(rootCA);
    CertificateProviderInterface certProvider =
        EncryptionInTransitUtil.getCertificateProviderInstance(rootCertConfigInfo, config);

    return certProvider.createCertificate(
        storagePath,
        username,
        certStart,
        certExpiry,
        CertificateHelper.CLIENT_CERT,
        CertificateHelper.CLIENT_KEY,
        null);
  }

  public static CertificateDetails createServerCertificate(
      Config config,
      UUID rootCA,
      String storagePath,
      String username,
      Date certStart,
      Date certExpiry,
      String certFileName,
      String certKeyName,
      Map<String, Integer> subjectAltNames) {
    log.info("Creating server certificate for {}", username);

    CertificateInfo rootCertConfigInfo = CertificateInfo.get(rootCA);
    CertificateProviderInterface certProvider =
        EncryptionInTransitUtil.getCertificateProviderInstance(rootCertConfigInfo, config);

    return certProvider.createCertificate(
        storagePath, username, certStart, certExpiry, certFileName, certKeyName, subjectAltNames);
  }

  public static GeneralNames extractGeneralNames(Map<String, Integer> subjectAltNames) {
    InetAddressValidator ipAddressValidator = InetAddressValidator.getInstance();
    List<GeneralName> altNames = new ArrayList<>();

    if (subjectAltNames == null) return null;

    for (Map.Entry<String, Integer> entry : subjectAltNames.entrySet()) {
      if (entry.getValue() == GeneralName.iPAddress) {
        // If IP address is invalid, throw error.
        if (!ipAddressValidator.isValid(entry.getKey())) {
          throw new IllegalArgumentException(
              String.format("IP %s invalid for SAN entry.", entry.getKey()));
        }
      }
      log.debug("Processing {}", entry.getKey());
      altNames.add(new GeneralName(entry.getValue(), entry.getKey()));
    }
    if (!altNames.isEmpty()) {
      return GeneralNames.getInstance(new DERSequence(altNames.toArray(new GeneralName[] {})));
    }
    return null;
  }

  public static String extractIPsFromGeneralNamesAsString(Map<String, Integer> subjectAltNames) {
    InetAddressValidator ipAddressValidator = InetAddressValidator.getInstance();
    StringBuilder ipAddrs = new StringBuilder();

    if (subjectAltNames == null) return null;

    for (Map.Entry<String, Integer> entry : subjectAltNames.entrySet()) {
      if (entry.getValue() == GeneralName.iPAddress) {
        // If IP address is invalid, throw error.
        if (!ipAddressValidator.isValid(entry.getKey())) {
          throw new IllegalArgumentException(
              String.format("IP %s invalid for SAN entry.", entry.getKey()));
        }
        ipAddrs.append(entry.getKey()).append(",");
      }
    }

    if (!Strings.isNullOrEmpty(ipAddrs.toString()))
      return ipAddrs.substring(0, ipAddrs.length() - 1);

    return "";
  }

  public static String extractHostNamesFromGeneralNamesAsString(
      Map<String, Integer> subjectAltNames) {
    StringBuilder names = new StringBuilder();

    if (subjectAltNames == null) return null;

    for (Map.Entry<String, Integer> entry : subjectAltNames.entrySet()) {
      if (entry.getValue() == GeneralName.dNSName) {
        names.append(entry.getKey()).append(",");
      }
    }

    if (!Strings.isNullOrEmpty(names.toString())) return names.substring(0, names.length() - 1);
    return "";
  }

  /**
   * Extract start and end dates from cert bundle/list
   *
   * @param x509CACerts
   * @return Pair of <StartDate, EndDate>
   */
  public static Pair<Date, Date> extractDatesFromCertBundle(List<X509Certificate> x509CACerts) {
    long certStartTimestamp = 0L;
    long certExpiryTimestamp = Long.MAX_VALUE;

    for (X509Certificate cert : x509CACerts) {
      if (cert.getNotBefore().getTime() > certStartTimestamp) {
        certStartTimestamp = cert.getNotBefore().getTime();
      }
      if (cert.getNotAfter().getTime() < certExpiryTimestamp) {
        certExpiryTimestamp = cert.getNotAfter().getTime();
      }
    }

    return new ImmutablePair<>(new Date(certStartTimestamp), new Date(certExpiryTimestamp));
  }

  public static UUID uploadRootCA(
      String label,
      UUID customerUUID,
      String storagePath,
      String certContent,
      String keyContent,
      CertConfigType certType,
      CertificateParams.CustomCertInfo customCertInfo,
      CertificateParams.CustomServerCertData customServerCertData) {
    log.debug("uploadRootCA: Label: {}, customerUUID: {}", label, customerUUID.toString());
    try {
      if (certContent == null) {
        throw new PlatformServiceException(BAD_REQUEST, "Certificate file can't be null");
      }
      UUID rootCA_UUID = UUID.randomUUID();
      String keyPath = null;
      CertificateInfo.CustomServerCertInfo customServerCertInfo = null;
      List<X509Certificate> x509CACerts;
      try {
        x509CACerts = convertStringToX509CertList(certContent);
      } catch (CertificateException e) {
        throw new PlatformServiceException(BAD_REQUEST, "Unable to get cert Objects");
      }

      Pair<Date, Date> dates = extractDatesFromCertBundle(x509CACerts);
      Date certStart = dates.getLeft();
      Date certExpiry = dates.getRight();

      // Verify the uploaded cert is a verified cert chain.
      verifyCertValidity(x509CACerts);
      if (certType == CertConfigType.SelfSigned) {
        // The first entry in the file should be the cert we want to use for generating server
        // certs.
        verifyCertSignatureAndOrder(x509CACerts, keyContent);
        keyPath = getCAKeyPath(storagePath, customerUUID, rootCA_UUID);

      } else if (certType == CertConfigType.CustomServerCert) {
        // Verify the upload Server Cert is a verified cert chain.
        List<X509Certificate> x509ServerCertificates;
        try {
          x509ServerCertificates =
              convertStringToX509CertList(customServerCertData.serverCertContent);
        } catch (CertificateException e) {
          throw new PlatformServiceException(BAD_REQUEST, "Unable to get cert Objects");
        }

        // Verify that the uploaded server cert was signed by the uploaded CA cert
        List<X509Certificate> combinedArrayList = new ArrayList<>(x509ServerCertificates);
        combinedArrayList.addAll(x509CACerts);
        verifyCertValidity(combinedArrayList);
        // The first entry in the file should be the cert we want to use for generating server
        // certs.
        verifyCertSignatureAndOrder(x509ServerCertificates, customServerCertData.serverKeyContent);
        String serverCertPath =
            String.format("%s/certs/%s/%s/%s", storagePath, customerUUID, rootCA_UUID, SERVER_CERT);
        String serverKeyPath =
            String.format("%s/certs/%s/%s/%s", storagePath, customerUUID, rootCA_UUID, SERVER_KEY);
        writeCertBundleToCertPath(x509ServerCertificates, serverCertPath);
        writeKeyFileContentToKeyPath(
            getPrivateKey(customServerCertData.serverKeyContent), serverKeyPath);
        customServerCertInfo =
            new CertificateInfo.CustomServerCertInfo(serverCertPath, serverKeyPath);
      } else if (certType == CertConfigType.HashicorpVault) {
        throw new PlatformServiceException(BAD_REQUEST, "Not a valid request for HashicorpVault");
      }
      String certPath = getCACertPath(storagePath, customerUUID, rootCA_UUID);
      writeCertBundleToCertPath(x509CACerts, certPath);

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
        case K8SCertManager:
          {
            keyPath = null;
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
        default:
          {
            throw new PlatformServiceException(BAD_REQUEST, "certType should be valid.");
          }
      }
      log.info(
          "Uploaded cert label {} (uuid {}) of type {} at paths"
              + " '{}', '{}' with custom cert info {}",
          label,
          rootCA_UUID,
          certType,
          certPath,
          keyPath,
          Json.toJson(customCertInfo));
      return cert.getUuid();
    } catch (IOException | NoSuchAlgorithmException e) {
      log.error(
          "uploadRootCA: Could not generate checksum for cert {} for customer {}",
          label,
          customerUUID);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "uploadRootCA: Checksum generation failed.");
    }
  }

  public static String getCertPEMFileContents(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    return FileUtils.readFileToString(new File(cert.getCertificate()));
  }

  public static String getCertPEM(UUID rootCA) {
    String certPEM = getCertPEMFileContents(rootCA);
    certPEM = Base64.getEncoder().encodeToString(certPEM.getBytes());
    return certPEM;
  }

  public static String getCertPEM(CertificateInfo cert) {
    String certPEM = FileUtils.readFileToString(new File(cert.getCertificate()));
    certPEM = Base64.getEncoder().encodeToString(certPEM.getBytes());
    return certPEM;
  }

  public static String getKeyPEM(CertificateInfo cert) {
    if (cert.getCertType() == CertConfigType.HashicorpVault
        || (cert.getCertType() == CertConfigType.K8SCertManager)) {
      return "";
    }
    String privateKeyPEM = FileUtils.readFileToString(new File(cert.getPrivateKey()));
    privateKeyPEM = Base64.getEncoder().encodeToString(privateKeyPEM.getBytes());
    return privateKeyPEM;
  }

  public static String getKeyPEM(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    return getKeyPEM(cert);
  }

  public static String getClientCertFile(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    File certFile = new File(cert.getCertificate());
    String path = certFile.getParentFile().toString();
    return String.format("%s/%s", path, CLIENT_CERT);
  }

  public static String getClientKeyFile(UUID rootCA) {
    CertificateInfo cert = CertificateInfo.get(rootCA);
    File certFile = new File(cert.getCertificate());
    String path = certFile.getParentFile().toString();
    return String.format("%s/%s", path, CLIENT_KEY);
  }

  public static boolean areCertsDiff(UUID cert1, UUID cert2) {
    try {
      CertificateInfo cer1 = CertificateInfo.get(cert1);
      CertificateInfo cer2 = CertificateInfo.get(cert2);
      FileInputStream is1 = new FileInputStream(cer1.getCertificate());
      FileInputStream is2 = new FileInputStream(cer2.getCertificate());
      CertificateFactory fact = CertificateFactory.getInstance("X.509");
      X509Certificate certObj1 = (X509Certificate) fact.generateCertificate(is1);
      X509Certificate certObj2 = (X509Certificate) fact.generateCertificate(is2);
      return !certObj2.equals(certObj1);
    } catch (IOException | CertificateException e) {
      log.error("Unable to read certs {}: {}", cert1.toString(), cert2.toString());
      throw new RuntimeException("Could not read certs to compare. " + e);
    }
  }

  public static boolean arePathsSame(UUID cert1, UUID cert2) {
    CertificateInfo cer1 = CertificateInfo.get(cert1);
    CertificateInfo cer2 = CertificateInfo.get(cert2);
    return (cer1.getCustomCertPathParams()
            .nodeCertPath
            .equals(cer2.getCustomCertPathParams().nodeCertPath)
        || cer1.getCustomCertPathParams()
            .nodeKeyPath
            .equals(cer2.getCustomCertPathParams().nodeKeyPath));
  }

  public static void createChecksums() {
    List<CertificateInfo> certs = CertificateInfo.getAllNoChecksum();
    for (CertificateInfo cert : certs) {
      try {
        cert.updateChecksum();
      } catch (IOException | NoSuchAlgorithmException e) {
        // Log error, but don't cause it to error out.
        log.error("Could not generate checksum for cert: {}", cert.getCertificate());
      }
    }
  }

  /**
   * return selected readable properties of certificate in form of string (k:v)
   *
   * @param cert
   * @return
   */
  public static String getCertificateProperties(X509Certificate cert) {

    String san = "";
    try {
      san = cert.getSubjectAlternativeNames().toString();
    } catch (Exception e) {
      san = "exception";
    }
    String ret;
    ret = "cert info: " + System.lineSeparator();
    ret += String.format("\t dn:%s", cert.getIssuerDN().toString());
    ret += String.format("\t subject:%s", cert.getSubjectDN().toString());
    ret += System.lineSeparator();
    ret += String.format("\t ip_san:%s", san);
    ret += System.lineSeparator();
    ret += String.format("\t beforeDate:%s", cert.getNotBefore().toString());
    ret += String.format("\t AfterDate:%s", cert.getNotAfter().toString());
    ret += System.lineSeparator();

    ret += String.format("\t serial:%s", cert.getSerialNumber().toString(16));
    return ret;
  }

  @SuppressWarnings("unchecked")
  public static List<X509Certificate> convertStringToX509CertList(String certContent)
      throws CertificateException {
    InputStream in;
    byte[] certEntryBytes = certContent.getBytes();
    in = new ByteArrayInputStream(certEntryBytes);
    CertificateFactory certFactory = CertificateFactory.getInstance("X.509");
    return (List<X509Certificate>) certFactory.generateCertificates(in);
  }

  public static X509Certificate convertStringToX509Cert(String certificate)
      throws CertificateException {
    certificate = certificate.replace("\\n", "");
    certificate = certificate.replaceAll("^\"+|\"+$", "");
    certificate = certificate.replace("-----BEGIN CERTIFICATE-----", "");
    certificate = certificate.replace("-----END CERTIFICATE-----", "");

    byte[] certificateData = Base64.getMimeDecoder().decode(certificate);
    CertificateFactory cf = CertificateFactory.getInstance("X509");
    return (X509Certificate) cf.generateCertificate(new ByteArrayInputStream(certificateData));
  }

  public static PrivateKey convertStringToPrivateKey(String strKey) throws Exception {
    strKey = strKey.replace(System.lineSeparator(), "");
    strKey = strKey.replaceAll("^\"+|\"+$", "");
    strKey = strKey.replace("-----BEGIN PRIVATE KEY-----", "");
    strKey = strKey.replace("-----END PRIVATE KEY-----", "");
    strKey = strKey.replace("-----BEGIN RSA PRIVATE KEY-----", "");
    strKey = strKey.replace("-----END RSA PRIVATE KEY-----", "");

    byte[] decoded = Base64.getMimeDecoder().decode(strKey);

    PKCS8EncodedKeySpec spec = new PKCS8EncodedKeySpec(decoded);
    KeyFactory kf = KeyFactory.getInstance("RSA");
    return kf.generatePrivate(spec);
  }

  public static PrivateKey getPrivateKey(String keyContent) {
    try (PemReader pemReader = new PemReader(new StringReader(keyContent))) {
      PemObject pemObject = pemReader.readPemObject();
      byte[] bytes = pemObject.getContent();
      PKCS8EncodedKeySpec spec = new PKCS8EncodedKeySpec(bytes);
      KeyFactory kf = KeyFactory.getInstance("RSA");
      return kf.generatePrivate(spec);
    } catch (Exception e) {
      log.error(e.getMessage());
      throw new RuntimeException("Unable to get Private Key");
    }
  }

  public static void writeCertFileContentToCertPath(X509Certificate cert, String certPath)
      throws IOException {
    writeCertFileContentToCertPath(cert, certPath, true, false);
  }

  public static void writeCertFileContentToCertPath(
      X509Certificate cert, String certPath, boolean syncToDB) throws IOException {
    writeCertFileContentToCertPath(cert, certPath, syncToDB, false);
  }

  public static void writeCertFileContentToCertPath(
      X509Certificate cert, String certPath, boolean syncToDB, boolean append) throws IOException {
    File certFile = new File(certPath);
    try (JcaPEMWriter certWriter = new JcaPEMWriter(new FileWriter(certFile, append))) {
      certWriter.writeObject(cert);
      certWriter.flush();

      if (syncToDB) {
        // Write the certificates in the DB.
        FileData.upsertFileInDB(certPath);
      }
    } catch (IOException e) {
      log.error(e.getMessage(), e);
      throw e;
    }
  }

  public static void writeKeyFileContentToKeyPath(Key keyContent, String keyPath) {
    writeKeyFileContentToKeyPath(keyContent, keyPath, true);
  }

  public static void writeKeyFileContentToKeyPath(
      Key keyContent, String keyPath, boolean syncToDB) {
    File keyFile = new File(keyPath);
    try (JcaPEMWriter keyWriter = new JcaPEMWriter(new FileWriter(keyFile))) {
      keyWriter.writeObject(keyContent);
      keyWriter.flush();

      if (syncToDB) {
        // Write the certificate private key in the DB.
        FileData.upsertFileInDB(keyPath);
      }
    } catch (Exception e) {
      log.error(e.getMessage());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, String.format("Failed to save private key: %s", e.getMessage()));
    }
  }

  public static void writeCertBundleToCertPath(List<X509Certificate> certs, String certPath) {
    writeCertBundleToCertPath(certs, certPath, true);
  }

  public static void writeCertBundleToCertPath(
      List<X509Certificate> certs, String certPath, boolean syncToDB) {
    writeCertBundleToCertPath(certs, certPath, syncToDB, false);
  }

  public static void writeCertBundleToCertPath(
      List<X509Certificate> certs, String certPath, boolean syncToDB, boolean append) {
    File certFile = new File(certPath);
    // Create directory to store the certFile.
    certFile.getParentFile().mkdirs();
    log.info("Dumping certs at path: {}", certPath);
    try (JcaPEMWriter certWriter = new JcaPEMWriter(new FileWriter(certFile, append))) {
      for (X509Certificate cert : certs) {
        log.info(getCertificateProperties(cert));
        certWriter.writeObject(cert);
        certWriter.flush();
      }

      if (syncToDB) {
        // Write the certificate in the DB.
        FileData.upsertFileInDB(certPath);
      }
    } catch (IOException e) {
      log.error(e.getMessage());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Saving certificate content failed");
    }
  }

  public static KeyPair getKeyPairObject()
      throws NoSuchAlgorithmException, NoSuchProviderException {
    KeyPairGenerator keypairGen = KeyPairGenerator.getInstance("RSA", "BC");
    keypairGen.initialize(2048);
    return keypairGen.generateKeyPair();
  }

  public static String getCertsNodeDir(String ybHomeDir) {
    return ybHomeDir + CERTS_NODE_SUBDIR;
  }

  public static String getCertsForClientDir(String ybHomeDir) {
    return ybHomeDir + CERT_CLIENT_NODE_SUBDIR;
  }

  public X509Certificate generateCACertificate(
      String certLabel, KeyPair keyPair, int expiryInYear) {
    try {
      Calendar cal = Calendar.getInstance();
      Date certStart = cal.getTime();
      cal.add(Calendar.YEAR, expiryInYear);
      Date certExpiry = cal.getTime();

      // Set runtime config to customize.
      String orgName = runtimeConfGetter.getGlobalConf(GlobalConfKeys.orgNameSelfSignedCert);
      if (StringUtils.isEmpty(orgName)) {
        orgName = "example.com";
      }

      X500Name subject =
          new X500NameBuilder(BCStyle.INSTANCE)
              .addRDN(BCStyle.CN, certLabel)
              .addRDN(BCStyle.O, orgName)
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
      return converter.getCertificate(holder);
    } catch (Exception e) {
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  public static X509Certificate createAndSignCertificate(
      String username,
      X500Name subject,
      KeyPair newCertKeyPair,
      X509Certificate caCert,
      PrivateKey caPrivateKey,
      Map<String, Integer> subjectAltNames,
      int maxLifetimeInYears) {
    try {
      X500Name newCertSubject = new X500Name(String.format("CN=%s", username));
      BigInteger newCertSerial = BigInteger.valueOf(System.currentTimeMillis());
      PKCS10CertificationRequestBuilder p10Builder =
          new JcaPKCS10CertificationRequestBuilder(newCertSubject, newCertKeyPair.getPublic());
      ContentSigner csrContentSigner =
          new JcaContentSignerBuilder(CertificateHelper.SIGNATURE_ALGO).build(caPrivateKey);
      PKCS10CertificationRequest csr = p10Builder.build(csrContentSigner);

      KeyUsage keyUsage =
          new KeyUsage(
              KeyUsage.digitalSignature
                  | KeyUsage.nonRepudiation
                  | KeyUsage.keyEncipherment
                  | KeyUsage.keyCertSign);

      Instant now = Instant.now();
      Date notBefore = Date.from(now);
      OffsetDateTime nowWithZoneOffset = now.atOffset(ZoneOffset.UTC);
      Date notAfter =
          Date.from(
              nowWithZoneOffset
                  .toLocalDate()
                  .plusYears(maxLifetimeInYears)
                  .atTime(nowWithZoneOffset.toLocalTime())
                  .toInstant(ZoneOffset.UTC));

      X509v3CertificateBuilder newCertBuilder =
          new X509v3CertificateBuilder(
              subject,
              newCertSerial,
              notBefore,
              notAfter.before(caCert.getNotAfter()) ? notAfter : caCert.getNotAfter(),
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

      newCert.verify(caCert.getPublicKey(), BouncyCastleProvider.PROVIDER_NAME);

      return newCert;
    } catch (Exception e) {
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  public static String getAsPemString(Object certObj) {
    try (StringWriter certOutput = new StringWriter();
        JcaPEMWriter certWriter = new JcaPEMWriter(certOutput)) {
      certWriter.writeObject(certObj);
      certWriter.flush();
      return certOutput.toString();
    } catch (Exception e) {
      log.error(e.getMessage(), e);
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  @SuppressWarnings("unchecked")
  public static Collection<X509Certificate> getCertsFromFile(String path) {
    try (FileInputStream in = new FileInputStream(path)) {
      CertificateFactory fact = CertificateFactory.getInstance("X.509");
      return (List<X509Certificate>) fact.generateCertificates(in);
    } catch (Exception e) {
      log.error("Failed to read cert file {}", path, e);
      throw new RuntimeException(e.getMessage(), e);
    }
  }

  private static boolean verifySignature(X509Certificate cert, String key) {
    try {
      // Add the security provider in case verifySignature was never called.
      getKeyPairObject();
      RSAPrivateKey privKey = (RSAPrivateKey) getPrivateKey(key);
      RSAPublicKey publicKey = (RSAPublicKey) cert.getPublicKey();
      return privKey.getModulus().toString().equals(publicKey.getModulus().toString());
    } catch (Exception e) {
      log.error("Cert or key is invalid." + e.getMessage());
    }
    return false;
  }

  // Verify that each certificate in the root chain has been signed by
  // another cert present in the uploaded file.
  public static void verifyCertValidity(List<X509Certificate> certs) {
    certs.forEach(
        cert -> {
          if (certs.stream()
              .noneMatch(potentialRootCert -> verifyCertValidity(cert, potentialRootCert))) {
            X500Name x500Name = new X500Name(cert.getSubjectX500Principal().getName());
            RDN cn = x500Name.getRDNs(BCStyle.CN)[0];
            throw new PlatformServiceException(
                BAD_REQUEST,
                "Certificate with CN = " + cn.getFirst().getValue() + " has no associated root");
          }
          verifyCertDateValidity(cert);
        });
  }

  // Verify that certificate is currently valid and valid for 1 day.
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
      // Exception means the verification failed.
      return false;
    }
  }

  private static boolean verifyCertSignatureAndOrder(
      List<X509Certificate> x509Certificates, String keyContent) {
    if (!verifySignature(x509Certificates.get(0), keyContent)) {
      // If the first certificate is not the right one, maybe the user has entered the
      // certificates in the wrong order. Check and update the customer with the right message.
      x509Certificates.forEach(
          x509Certificate -> {
            if (verifySignature(x509Certificate, keyContent)) {
              X500Name x500Name = new X500Name(x509Certificate.getSubjectX500Principal().getName());
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

  public static boolean checkNode2NodeCertsExpiry(Universe universe) {
    HealthCheck lastHealthCheck = HealthCheck.getLatest(universe.getUniverseUUID());
    if (lastHealthCheck != null) {
      HealthCheck.Details details = lastHealthCheck.getDetailsJson();
      List<HealthCheck.Details.NodeData> dataNodes = details.getData();

      dataNodes =
          dataNodes.stream()
              .filter(
                  node ->
                      certsListForValidityCheck.stream()
                          .anyMatch(key -> node.getMessage().toString().contains(key)))
              .filter(HealthCheck.Details.NodeData::getHasError)
              .filter(node -> node.getDetails().toString().contains("certificate expired"))
              .collect(Collectors.toList());

      return dataNodes.size() > 0;
    }

    return false;
  }

  public static Boolean isValidRsaKey(String privateKeyString) {
    try {
      return getPrivateKey(privateKeyString).getAlgorithm().equals("RSA");
    } catch (RuntimeException e) {
      log.error("Private key Algorithm extraction failed: ", e);
      return false;
    }
  }
}
