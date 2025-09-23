// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.FileData;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class YBATrustStoreManager implements TrustStoreManager {

  public static final String JVM_DEFAULT_KEYSTORE_TYPE = "JKS";

  public static final String BCFKS_TRUSTSTORE_FILE_NAME = "ybBcfksCaCerts";

  public static final String PKCS12_TRUSTSTORE_FILE_NAME = "ybPkcs12CaCerts";
  private static final String YB_JAVA_HOME_PATHS = "yb.wellKnownCA.trustStore.javaHomePaths";

  private final RuntimeConfGetter runtimeConfGetter;

  private final Config config;

  @Inject
  public YBATrustStoreManager(RuntimeConfGetter runtimeConfGetter, Config config) {
    this.runtimeConfGetter = runtimeConfGetter;
    this.config = config;
  }

  /** Creates a trust-store with only custom CA certificates in pkcs12 format. */
  public boolean addCertificate(
      String certPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {

    log.debug("Trying to update YBA's truststore ...");
    // Get the existing trust bundle.
    TrustStoreInfo trustStoreInfo = getYbaTrustStoreInfo(trustStoreHome);
    log.debug("Updating truststore {}", trustStoreInfo);

    boolean doesTrustStoreExist = new File(trustStoreInfo.getPath()).exists();
    KeyStore trustStore = null;
    if (!doesTrustStoreExist) {
      File trustStoreFile = new File(trustStoreInfo.getPath());
      trustStoreFile.createNewFile();
      log.debug("Created an empty YBA trust-store");
    }

    trustStore = getTrustStore(trustStoreInfo, trustStorePassword, !doesTrustStoreExist);
    if (trustStore == null) {
      String errMsg = "Truststore cannot be null";
      log.error(errMsg);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errMsg);
    }

    // Check if such an alias already exists.
    if (trustStore.containsAlias(certAlias) && !suppressErrors) {
      String errMsg = String.format("CA certificate by name '%s' already exists", certAlias);
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    List<Certificate> certificates = getX509Certificate(certPath);
    for (int i = 0; i < certificates.size(); i++) {
      String alias = certAlias + "-" + i;
      trustStore.setCertificateEntry(alias, certificates.get(i));
    }
    // Update the trust store in file-system.
    saveTrustStore(trustStoreInfo, trustStore, trustStorePassword);
    log.debug(
        "Truststore '{}' now has a certificate with alias '{}'",
        trustStoreInfo.getPath(),
        certAlias);

    // Backup up YBA's pkcs12 trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStoreInfo.getPath()));

    log.info("Custom CA certificate added in YBA's pkcs12 trust-store");
    return !doesTrustStoreExist;
  }

  public void replaceCertificate(
      String oldCertPath,
      String newCertPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws IOException, KeyStoreException, CertificateException {

    // Get the existing trust bundle.
    TrustStoreInfo trustStoreInfo = getYbaTrustStoreInfo(trustStoreHome);
    log.debug("Trying to replace cert {} in YBA's truststore {}", certAlias, trustStoreInfo);
    KeyStore trustStore = getTrustStore(trustStoreInfo, trustStorePassword, false);
    if (trustStore == null) {
      String errMsg = "Truststore cannot be null";
      log.error(errMsg);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errMsg);
    }

    List<Certificate> oldCertificates = getX509Certificate(oldCertPath);
    List<Certificate> newCertificates = getX509Certificate(newCertPath);
    for (int i = 0; i < oldCertificates.size(); i++) {
      // Check if such an alias already exists.
      String alias = certAlias + "-" + i;
      if (!trustStore.containsAlias(alias) && !suppressErrors) {
        String errMsg = String.format("Cert by name '%s' does not exist to update", alias);
        log.error(errMsg);
        // Purge newCertPath which got created.
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
    }

    if (newCertificates.size() < oldCertificates.size()) {
      for (int i = newCertificates.size(); i < oldCertificates.size(); i++) {
        String alias = certAlias + "-" + i;
        trustStore.deleteEntry(alias);
      }
    }

    // Update the trust store.
    for (int i = 0; i < newCertificates.size(); i++) {
      String alias = certAlias + "-" + i;
      trustStore.setCertificateEntry(alias, newCertificates.get(i));
    }
    saveTrustStore(trustStoreInfo, trustStore, trustStorePassword);

    // Backup up YBA's pkcs12 trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStoreInfo.getPath()));

    log.info(
        "Truststore '{}' updated with new cert at alias '{}'", trustStoreInfo.getPath(), certAlias);
  }

  private void saveTrustStore(
      TrustStoreInfo trustStoreInfo, KeyStore trustStore, char[] trustStorePassword) {
    if (trustStore != null) {
      try (FileOutputStream storeOutputStream = new FileOutputStream(trustStoreInfo.getPath())) {
        trustStore.store(storeOutputStream, trustStorePassword);
        log.debug("Trust store written to {}", trustStoreInfo.getPath());
      } catch (IOException
          | KeyStoreException
          | NoSuchAlgorithmException
          | CertificateException e) {
        String msg = String.format("Failed to save certificate to %s", trustStoreInfo.getPath());
        log.error(msg, e);
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
      }
    }
  }

  protected KeyStore getTrustStore(
      TrustStoreInfo trustStoreInfo, char[] trustStorePassword, boolean init) {
    try (FileInputStream storeInputStream = new FileInputStream(trustStoreInfo.getPath())) {
      KeyStore trustStore = KeyStore.getInstance(trustStoreInfo.getType());
      if (init) {
        trustStore.load(null, trustStorePassword);
      } else {
        trustStore.load(storeInputStream, trustStorePassword);
      }
      return trustStore;
    } catch (IOException | KeyStoreException | NoSuchAlgorithmException | CertificateException e) {
      String msg =
          String.format("Failed to get pkcs12 trust store. Error %s", e.getLocalizedMessage());
      log.error(msg, e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
    }
  }

  protected KeyStore getTrustStore(String trustStorePath, char[] trustStorePassword, String type) {
    KeyStore trustStore = null;
    try (FileInputStream storeInputStream = new FileInputStream(trustStorePath)) {
      trustStore = KeyStore.getInstance(type);
      trustStore.load(storeInputStream, trustStorePassword);
    } catch (Exception e) {
      throw new RuntimeException("Couldn't load trust store " + trustStorePath, e);
    }
    return trustStore;
  }

  public void remove(
      String certPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws KeyStoreException, IOException, CertificateException {
    log.info("Removing cert {} from YBA's pkcs12 truststore ...", certAlias);

    TrustStoreInfo trustStoreInfo = getYbaTrustStoreInfo(trustStoreHome);
    KeyStore trustStore = getTrustStore(trustStoreInfo, trustStorePassword, false);
    List<Certificate> certificates = getX509Certificate(certPath);
    for (int i = 0; i < certificates.size(); i++) {
      String alias = certAlias + "-" + i;

      // Check if such an alias already exists.
      if (!trustStore.containsAlias(alias) && !suppressErrors) {
        String errMsg = String.format("CA certificate '%s' does not exist to delete", alias);
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }

      // Delete from the trust store.
      if (trustStore.containsAlias(alias)) {
        trustStore.deleteEntry(alias);
      }
    }
    saveTrustStore(trustStoreInfo, trustStore, trustStorePassword);
    log.debug(
        "Truststore '{}' now does not have a CA certificate '{}'",
        trustStoreInfo.getPath(),
        certAlias);

    log.info("Custom CA certs deleted in YBA's pkcs12 truststore");
  }

  // ------------- methods for Java defaults ----------------
  private Map<String, String> maybeGetJavaxNetSslTrustStore() {
    String javaxNetSslTrustStore =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.javaxNetSslTrustStore);
    String javaxNetSslTrustStoreType =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.javaxNetSslTrustStoreType);
    String javaxNetSslTrustStorePassword =
        runtimeConfGetter.getGlobalConf(GlobalConfKeys.javaxNetSslTrustStorePassword);
    log.debug(
        "Javax truststore is: {}, type is: {}", javaxNetSslTrustStore, javaxNetSslTrustStoreType);
    if (!com.cronutils.utils.StringUtils.isEmpty(javaxNetSslTrustStore)
        && Files.exists(Paths.get(javaxNetSslTrustStore))) {
      Map<String, String> javaxNetSslMap = new HashMap<>();
      javaxNetSslMap.put("path", javaxNetSslTrustStore);
      if (!com.cronutils.utils.StringUtils.isEmpty(javaxNetSslTrustStoreType)) {
        javaxNetSslMap.put("type", javaxNetSslTrustStoreType);
      }
      if (!com.cronutils.utils.StringUtils.isEmpty(javaxNetSslTrustStorePassword)) {
        javaxNetSslMap.put("password", javaxNetSslTrustStorePassword);
      }
      return javaxNetSslMap;
    }
    return null;
  }

  protected Map<String, String> getJavaDefaultConfig() {
    // Java looks for trust-store in these files by default in this order.
    // NOTE: If adding any custom path, we must add the ordered default path as well, if they exist.
    Map<String, String> javaxNetSslMap = maybeGetJavaxNetSslTrustStore();
    if (javaxNetSslMap != null) {
      return javaxNetSslMap;
    }

    Map<String, String> javaSSLConfigMap = new HashMap<>();
    List<String> javaHomePaths = config.getStringList(YB_JAVA_HOME_PATHS);
    log.debug("Java home certificate paths are {}", javaHomePaths);
    for (String javaPath : javaHomePaths) {
      if (Files.exists(Paths.get(javaPath))) {
        javaSSLConfigMap.put("path", javaPath);
        javaSSLConfigMap.put("type", JVM_DEFAULT_KEYSTORE_TYPE); // pkcs12
      }
    }
    log.info("Java SSL config is:{}", javaSSLConfigMap);
    return javaSSLConfigMap;
  }

  protected KeyStore getJavaDefaultKeystore() {
    KeyStore javaStore = null;
    Map<String, String> javaxNetSslMap = maybeGetJavaxNetSslTrustStore();

    // Java looks for trust-store in these files by default in this order.
    // NOTE: If adding any custom path, we must add the ordered default path as well, if they exist.
    if (javaxNetSslMap != null) {
      javaStore =
          getTrustStore(
              javaxNetSslMap.get("path"),
              javaxNetSslMap.get("password").toCharArray(),
              JVM_DEFAULT_KEYSTORE_TYPE);
      return javaStore;
    }

    List<String> javaHomePaths = config.getStringList(YB_JAVA_HOME_PATHS);
    log.debug("Java home cert paths are {}", javaHomePaths);
    for (String javaPath : javaHomePaths) {
      if (Files.exists(Paths.get(javaPath))) {
        javaStore = getTrustStore(javaPath, null, "JKS");
        break;
      }
    }
    return javaStore;
  }

  public TrustStoreInfo getYbaTrustStoreInfo(String trustStoreHome) {
    // Get the existing trust bundle.
    String trustStorePath = getTrustStorePath(trustStoreHome, PKCS12_TRUSTSTORE_FILE_NAME);
    if (Files.exists(Path.of(trustStorePath))) {
      // PKSC12 bundle for backward compatibility
      return new TrustStoreInfo(trustStorePath, "PKCS12");
    }
    // BCFKS bundle for fresh installed YBAs to simplify FIPS migration
    return new TrustStoreInfo(
        getTrustStorePath(trustStoreHome, BCFKS_TRUSTSTORE_FILE_NAME), "BCFKS");
  }
}
