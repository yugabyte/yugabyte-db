// Copyright (c) YugaByte, Inc.

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
public class Pkcs12TrustStoreManager implements TrustStoreManager {

  public static final String TRUSTSTORE_FILE_NAME = "ybPkcs12CaCerts";
  private static final String YB_JAVA_HOME_PATHS = "yb.wellKnownCA.trustStore.javaHomePaths";

  private final RuntimeConfGetter runtimeConfGetter;

  @Inject Config config;

  @Inject
  public Pkcs12TrustStoreManager(RuntimeConfGetter runtimeConfGetter) {
    this.runtimeConfGetter = runtimeConfGetter;
  }

  /** Creates a trust-store with only custom CA certificates in pkcs12 format. */
  public boolean addCertificate(
      String certPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {

    log.debug("Trying to update YBA's pkcs12 truststore ...");
    // Get the existing trust bundle.
    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    log.debug("Updating truststore {}", trustStorePath);

    boolean doesTrustStoreExist = new File(trustStorePath).exists();
    KeyStore trustStore = null;
    if (!doesTrustStoreExist) {
      File trustStoreFile = new File(trustStorePath);
      trustStoreFile.createNewFile();
      log.debug("Created an empty YBA pkcs12 trust-store");
    }

    trustStore = getTrustStore(trustStorePath, trustStorePassword, !doesTrustStoreExist);
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
    saveTrustStore(trustStorePath, trustStore, trustStorePassword);
    log.debug("Truststore '{}' now has a certificate with alias '{}'", trustStorePath, certAlias);

    // Backup up YBA's pkcs12 trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStorePath));

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
    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    log.debug("Trying to replace cert {} in YBA's pkcs12 truststore {}", certAlias, trustStorePath);
    KeyStore trustStore = getTrustStore(trustStorePath, trustStorePassword, false);
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
    saveTrustStore(trustStorePath, trustStore, trustStorePassword);

    // Backup up YBA's pkcs12 trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStorePath));

    log.info("Truststore '{}' updated with new cert at alias '{}'", trustStorePath, certAlias);
  }

  private void saveTrustStore(
      String trustStorePath, KeyStore trustStore, char[] trustStorePassword) {
    if (trustStore != null) {
      try (FileOutputStream storeOutputStream = new FileOutputStream(trustStorePath)) {
        trustStore.store(storeOutputStream, trustStorePassword);
        log.debug("Trust store written to {}", trustStorePath);
      } catch (IOException
          | KeyStoreException
          | NoSuchAlgorithmException
          | CertificateException e) {
        String msg = String.format("Failed to save certificate to %s", trustStorePath);
        log.error(msg, e);
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
      }
    }
  }

  protected KeyStore getTrustStore(String trustStorePath, char[] trustStorePassword, boolean init) {
    try (FileInputStream storeInputStream = new FileInputStream(trustStorePath)) {
      KeyStore trustStore = KeyStore.getInstance(KeyStore.getDefaultType());
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

  protected KeyStore maybeGetTrustStore(String trustStorePath, char[] trustStorePassword) {
    KeyStore trustStore = null;
    try (FileInputStream storeInputStream = new FileInputStream(trustStorePath)) {
      trustStore = KeyStore.getInstance(KeyStore.getDefaultType());
      trustStore.load(storeInputStream, trustStorePassword);
    } catch (Exception e) {
      log.warn(String.format("Couldn't get pkcs12 trust store: %s", e.getLocalizedMessage()));
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

    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    KeyStore trustStore = getTrustStore(trustStorePath, trustStorePassword, false);
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
    saveTrustStore(trustStorePath, trustStore, trustStorePassword);
    log.debug("Truststore '{}' now does not have a CA certificate '{}'", trustStorePath, certAlias);

    log.info("Custom CA certs deleted in YBA's pkcs12 truststore");
  }

  public String getYbaTrustStorePath(String trustStoreHome) {
    // Get the existing trust bundle.
    return getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
  }

  public String getYbaTrustStoreType() {
    String storeType = KeyStore.getDefaultType();
    log.debug("The trust-store type is {}", storeType); // pkcs12
    return storeType;
  }

  public boolean isTrustStoreEmpty(String caStorePathStr, char[] trustStorePassword) {
    KeyStore trustStore = maybeGetTrustStore(caStorePathStr, trustStorePassword);
    if (trustStore == null) {
      return true;
    } else {
      try {
        log.debug("There are {} entries in pkcs12 trust-store", trustStore.size());
        if (trustStore.size() == 0) {
          return true;
        }
      } catch (KeyStoreException e) {
        String msg = "Failed to get size of pkcs12 trust-store";
        log.error(msg, e.getLocalizedMessage());
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getLocalizedMessage());
      }
    }
    return false;
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
        javaSSLConfigMap.put("type", KeyStore.getDefaultType()); // pkcs12
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
          maybeGetTrustStore(
              javaxNetSslMap.get("path"), javaxNetSslMap.get("password").toCharArray());
      return javaStore;
    }

    List<String> javaHomePaths = config.getStringList(YB_JAVA_HOME_PATHS);
    log.debug("Java home cert paths are {}", javaHomePaths);
    for (String javaPath : javaHomePaths) {
      if (Files.exists(Paths.get(javaPath))) {
        javaStore = maybeGetTrustStore(javaPath, null);
      }
    }
    return javaStore;
  }
}
