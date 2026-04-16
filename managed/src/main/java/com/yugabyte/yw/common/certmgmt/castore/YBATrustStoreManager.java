// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.ImmutableList;
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
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;

@Singleton
@Slf4j
public class YBATrustStoreManager implements TrustStoreManager {

  public static final String KEYSTORE_TYPE_JKS = "JKS";
  public static final String KEYSTORE_TYPE_PKCS12 = "PKCS12";
  public static final String KEYSTORE_TYPE_BCFKS = "BCFKS";

  public static final String BCFKS_TRUSTSTORE_FILE_NAME = "ybBcfksCaCerts";

  public static final String PKCS12_TRUSTSTORE_FILE_NAME = "ybPkcs12CaCerts";
  public static final String PKCS12_TRUSTSTORE_CONVERTED_FILE_NAME = "ybPkcs12CaCerts.backup";
  private static final String YB_JAVA_HOME_PATHS = "yb.wellKnownCA.trustStore.javaHomePaths";

  private final RuntimeConfGetter runtimeConfGetter;

  private final Config config;

  private TrustStoreInfo ybaTrustStoreInfo;
  private TrustStoreInfo javaTrustStoreInfo;

  @Inject
  public YBATrustStoreManager(RuntimeConfGetter runtimeConfGetter, Config config) {
    this.runtimeConfGetter = runtimeConfGetter;
    this.config = config;
  }

  /** Creates a trust-store with only custom CA certificates. */
  public void addCertificate(
      String certPath, String certAlias, String trustStoreHome, boolean suppressErrors)
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {

    log.debug("Trying to update YBA truststore ...");
    // Get the existing trust bundle.
    TrustStoreInfo trustStoreInfo = getYbaTrustStoreInfo(trustStoreHome);
    log.debug("Updating truststore {}", trustStoreInfo);

    KeyStore trustStore = getTrustStore(trustStoreInfo, true, true);
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
    saveTrustStore(trustStoreInfo, trustStore);
    log.debug(
        "Truststore '{}' now has a certificate with alias '{}'",
        trustStoreInfo.getPath(),
        certAlias);

    // Backup up YBA trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStoreInfo.getPath()));

    log.info("Custom CA certificate added in YBA trust-store");
  }

  public void replaceCertificate(
      String oldCertPath,
      String newCertPath,
      String certAlias,
      String trustStoreHome,
      boolean suppressErrors)
      throws IOException, KeyStoreException, CertificateException {

    // Get the existing trust bundle.
    TrustStoreInfo trustStoreInfo = getYbaTrustStoreInfo(trustStoreHome);
    log.debug("Trying to replace cert {} in YBA truststore {}", certAlias, trustStoreInfo);
    KeyStore trustStore = getTrustStore(trustStoreInfo, false, true);
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
    saveTrustStore(trustStoreInfo, trustStore);

    // Backup up YBA trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStoreInfo.getPath()));

    log.info(
        "Truststore '{}' updated with new cert at alias '{}'", trustStoreInfo.getPath(), certAlias);
  }

  private void saveTrustStore(TrustStoreInfo trustStoreInfo, KeyStore trustStore) {
    if (trustStore != null) {
      try (FileOutputStream storeOutputStream = new FileOutputStream(trustStoreInfo.getPath())) {
        trustStore.store(storeOutputStream, trustStoreInfo.getPassword().toCharArray());
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

  protected KeyStore getTrustStore(TrustStoreInfo trustStoreInfo, boolean init, boolean logError) {
    try {
      KeyStore trustStore = KeyStore.getInstance(trustStoreInfo.getType());
      char[] password =
          trustStoreInfo.getPassword() != null ? trustStoreInfo.getPassword().toCharArray() : null;
      if (init && !Files.exists(Path.of(trustStoreInfo.getPath()))) {
        trustStore.load(null, password);
      } else {
        try (FileInputStream storeInputStream = new FileInputStream(trustStoreInfo.getPath())) {
          trustStore.load(storeInputStream, password);
        }
      }
      return trustStore;
    } catch (IOException | KeyStoreException | NoSuchAlgorithmException | CertificateException e) {
      String msg = String.format("Failed to get trust store. Error %s", e.getLocalizedMessage());
      if (logError) {
        log.error(msg, e);
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
    }
  }

  @Override
  public void remove(
      String certPath, String certAlias, String trustStoreHome, boolean suppressErrors)
      throws KeyStoreException, IOException, CertificateException {
    log.info("Removing cert {} from YBA truststore ...", certAlias);

    TrustStoreInfo trustStoreInfo = getYbaTrustStoreInfo(trustStoreHome);
    KeyStore trustStore = getTrustStore(trustStoreInfo, false, true);
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
    saveTrustStore(trustStoreInfo, trustStore);
    log.debug(
        "Truststore '{}' now does not have a CA certificate '{}'",
        trustStoreInfo.getPath(),
        certAlias);

    log.info("Custom CA certs deleted from YBA truststore");
  }

  protected Map<String, String> getJavaDefaultConfig() {
    TrustStoreInfo info = getJavaTrustStoreInfo();
    return info.toPlayConfig();
  }

  protected KeyStore getJavaDefaultKeystore() {
    TrustStoreInfo info = getJavaTrustStoreInfo();
    return getTrustStore(info, false, true);
  }

  public TrustStoreInfo getYbaTrustStoreInfo(String trustStoreHome) {
    if (ybaTrustStoreInfo != null) {
      return ybaTrustStoreInfo;
    }
    synchronized (this) {
      if (ybaTrustStoreInfo != null) {
        return ybaTrustStoreInfo;
      }

      TrustStoreInfo trustStoreInfo =
          new TrustStoreInfo(
              getTrustStorePath(trustStoreHome, BCFKS_TRUSTSTORE_FILE_NAME),
              KEYSTORE_TYPE_BCFKS,
              getTruststorePassword());

      String legacyStorePath = getTrustStorePath(trustStoreHome, PKCS12_TRUSTSTORE_FILE_NAME);
      if (Files.exists(Path.of(legacyStorePath))) {
        log.info("Legacy truststore file {} exists - converting to BCFKS", legacyStorePath);
        // Convert it to BCFKS for backward compatibility.
        List<TrustStoreInfo> candidates =
            ImmutableList.of(
                new TrustStoreInfo(
                    getTrustStorePath(trustStoreHome, PKCS12_TRUSTSTORE_FILE_NAME),
                    KEYSTORE_TYPE_PKCS12,
                    getTruststorePassword()),
                new TrustStoreInfo(
                    getTrustStorePath(trustStoreHome, PKCS12_TRUSTSTORE_FILE_NAME),
                    KEYSTORE_TYPE_JKS,
                    getTruststorePassword()));
        ImmutablePair<TrustStoreInfo, KeyStore> loadedStore = loadFirstOf(candidates);
        if (loadedStore == null) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Failed to load legacy keystore file");
        }
        KeyStore storeToConvert = loadedStore.getRight();
        KeyStore convertedStore = getTrustStore(trustStoreInfo, true, true);
        try {
          storeToConvert
              .aliases()
              .asIterator()
              .forEachRemaining(
                  alias -> {
                    try {
                      convertedStore.setCertificateEntry(
                          alias, storeToConvert.getCertificate(alias));
                    } catch (KeyStoreException e) {
                      throw new RuntimeException(
                          "Failed to convert certificate with alias " + alias, e);
                    }
                  });
        } catch (KeyStoreException e) {
          log.error("Failed to convert keystore to BCFKS format", e);
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Failed to convert keystore to BCFKS format");
        }
        saveTrustStore(trustStoreInfo, convertedStore);
        try {
          // Backup up converted YBA trust store in DB and remove legacy from backup.
          FileData.upsertFileInDB(trustStoreInfo.getPath(), false);
          File legacyKeystoreFile = new File(loadedStore.getLeft().getPath());
          File legacyKeystoreBackupFile =
              new File(getTrustStorePath(trustStoreHome, PKCS12_TRUSTSTORE_CONVERTED_FILE_NAME));
          FileUtils.moveFile(legacyKeystoreFile, legacyKeystoreBackupFile);
          FileData.deleteFileFromDB(legacyKeystoreFile.getPath());
          // Just in case also store backup of old truststore in the DB.
          FileData.upsertFileInDB(legacyKeystoreBackupFile.getPath(), false);
        } catch (IOException e) {
          log.error("Failed to backup converted keystore file {}", trustStoreInfo.getPath(), e);
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Failed to backup converted keystore file");
        }
      }
      this.ybaTrustStoreInfo = trustStoreInfo;
      return ybaTrustStoreInfo;
    }
  }

  public TrustStoreInfo getJavaTrustStoreInfo() {
    if (javaTrustStoreInfo != null) {
      return javaTrustStoreInfo;
    }
    synchronized (this) {
      if (javaTrustStoreInfo != null) {
        return javaTrustStoreInfo;
      }

      List<TrustStoreInfo> candidates = new ArrayList<>();

      // Java looks for trust-store in these files by default in this order.
      // NOTE: If adding any custom path, we must add the ordered default path as well, if they
      // exist.
      String javaxNetSslTrustStore =
          runtimeConfGetter.getGlobalConf(GlobalConfKeys.javaxNetSslTrustStore);
      String javaxNetSslTrustStoreType =
          runtimeConfGetter.getGlobalConf(GlobalConfKeys.javaxNetSslTrustStoreType);
      String javaxNetSslTrustStorePassword =
          runtimeConfGetter.getGlobalConf(GlobalConfKeys.javaxNetSslTrustStorePassword);
      log.debug(
          "Javax truststore is: {}, type is: {}", javaxNetSslTrustStore, javaxNetSslTrustStoreType);
      if (StringUtils.isNotEmpty(javaxNetSslTrustStore)
          && Files.exists(Paths.get(javaxNetSslTrustStore))) {
        if (StringUtils.isNotEmpty(javaxNetSslTrustStoreType)) {
          candidates.add(
              new TrustStoreInfo(
                  javaxNetSslTrustStore, javaxNetSslTrustStoreType, javaxNetSslTrustStorePassword));
        } else {
          // Try both legacy types
          candidates.add(
              new TrustStoreInfo(
                  javaxNetSslTrustStore, KEYSTORE_TYPE_JKS, javaxNetSslTrustStorePassword));
          candidates.add(
              new TrustStoreInfo(
                  javaxNetSslTrustStore, KEYSTORE_TYPE_PKCS12, javaxNetSslTrustStorePassword));
        }
      }

      List<String> javaHomePaths = config.getStringList(YB_JAVA_HOME_PATHS);
      log.debug("Java home certificate paths are {}", javaHomePaths);
      for (String javaPath : javaHomePaths) {
        // Try both types - as JVMs can use both in theory
        candidates.add(new TrustStoreInfo(javaPath, KEYSTORE_TYPE_JKS, null));
        candidates.add(new TrustStoreInfo(javaPath, KEYSTORE_TYPE_PKCS12, null));
      }

      ImmutablePair<TrustStoreInfo, KeyStore> loadedStore = loadFirstOf(candidates);
      if (loadedStore == null) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Failed to load java keystore file");
      }
      this.javaTrustStoreInfo = loadedStore.getLeft();
      return javaTrustStoreInfo;
    }
  }

  private ImmutablePair<TrustStoreInfo, KeyStore> loadFirstOf(List<TrustStoreInfo> candidates) {
    for (TrustStoreInfo candidate : candidates) {
      log.info("Attempting to load truststore {}", candidate);
      try {
        if (!Files.exists(Path.of(candidate.getPath()))) {
          log.warn("Truststore file {} does not exist", candidate);
          continue;
        }
        KeyStore loaded = getTrustStore(candidate, false, false);
        log.info("Successfully loaded {}", candidate);
        return ImmutablePair.of(candidate, loaded);
      } catch (Exception e) {
        log.warn("Failed to load {}", candidate, e);
      }
    }
    return null;
  }

  private String getTruststorePassword() {
    return "global-truststore-password";
  }
}
