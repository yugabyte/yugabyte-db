// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.CustomTrustStoreListener;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.CustomCaCertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.FileData;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class CustomCAStoreManager {
  private final String CUSTOM_CA_STORE_ENABLED = "yb.customCATrustStore.enabled";
  private final List<TrustStoreManager> trustStoreManagers = new ArrayList<>();

  // Reference to the listeners who want to get notified about updates in this custom trust-store.
  private final List<CustomTrustStoreListener> trustStoreListeners = new ArrayList<>();

  private final PemTrustStoreManager pemTrustStoreManager;
  private final Pkcs12TrustStoreManager pkcs12TrustStoreManager;

  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public CustomCAStoreManager(
      Pkcs12TrustStoreManager pkcs12TrustStoreManager,
      PemTrustStoreManager pemTrustStoreManager,
      RuntimeConfigFactory runtimeConfigFactory) {
    this.runtimeConfigFactory = runtimeConfigFactory;
    trustStoreManagers.add(pkcs12TrustStoreManager);
    trustStoreManagers.add(pemTrustStoreManager);
    this.pemTrustStoreManager = pemTrustStoreManager;
    this.pkcs12TrustStoreManager = pkcs12TrustStoreManager;
  }

  public UUID addCACert(UUID customerId, String name, String contents, String storagePath) {
    Customer.getOrBadRequest(customerId);

    boolean suppressErrors = false;
    UUID certId = UUID.randomUUID();
    char[] trustStorePassword = getTruststorePassword();
    log.info("Uploading custom CA certificate => name: '{}', customerId: '{}'", name, customerId);

    List<TrustStoreManager> trustStoreManagersToRollback = new ArrayList<>();
    List<X509Certificate> x509CACerts = null;
    try {
      x509CACerts = getCertChain(customerId, name, contents);
      validateNewCert(name, x509CACerts);
    } catch (CertificateException e) {
      log.error(String.format("Failed to validate certificate %s", name), e);
      throw new PlatformServiceException(BAD_REQUEST, e.getLocalizedMessage());
    }

    String trustStoreHome = getTruststoreHome(storagePath);
    String certPath = getCustomCACertsPath(trustStoreHome, certId);
    try {
      CertificateHelper.writeCertBundleToCertPath(x509CACerts, certPath);
      log.debug("CA certificate {} written to local filesystem and DB", certId);
      for (TrustStoreManager caMgr : trustStoreManagers) {
        caMgr.addCertificate(certPath, name, trustStoreHome, trustStorePassword, suppressErrors);
        trustStoreManagersToRollback.add(caMgr);
      }

      // Add to DB.
      boolean activeCert = true;
      CustomCaCertificateInfo.create(
          customerId,
          certId,
          name,
          certPath,
          x509CACerts.get(0).getNotBefore(),
          x509CACerts.get(0).getNotAfter(),
          activeCert);
      log.debug("Added CA certificate {} to DB", certId);

    } catch (Exception e) {
      suppressErrors = true; // Rollback errors need not be reported to user.
      try {
        boolean deleted = CustomCaCertificateInfo.delete(customerId, certId);
        log.debug(deleted ? "DB rolled back" : "Nothing to rollback in DB");
        for (TrustStoreManager tsm : trustStoreManagersToRollback) {
          tsm.remove(certPath, name, trustStoreHome, trustStorePassword, suppressErrors);
        }
        log.debug("Rolled back runtime trust-stores");
        purge(new File(certPath).getParentFile());
        log.debug("Rolled back filesystem");
      } catch (KeyStoreException | CertificateException | IOException ex) {
        log.warn("Cannot rollback add CA certificate", ex);
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getLocalizedMessage());
    }

    notifyListeners();
    log.debug("All trust-store listeners notified");

    return certId;
  }

  private void validateNewCert(String name, List<X509Certificate> x509CACerts) {
    CustomCaCertificateInfo cert = CustomCaCertificateInfo.getByName(name);
    if (cert != null) {
      String errMsg = String.format("CA certificate by name '%s' already exists", name);
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    // Verify the uploaded cert has a verified cert-chain and not expired.
    CertificateHelper.verifyCertValidity(x509CACerts);
  }

  public UUID updateCA(
      UUID customerId, UUID oldCertId, String name, String newCertContents, String storagePath) {

    Customer.getOrBadRequest(customerId);
    CustomCaCertificateInfo oldCert = validateAndGetCert(oldCertId, customerId, name);
    log.info("Refreshing custom CA => name: '{}', customerId: '{}' ...", name, customerId);

    // Validate input of old cert which will get updated.
    String trustStoreHome = getTruststoreHome(storagePath);
    String oldCertPath = getCustomCACertsPath(trustStoreHome, oldCertId);
    if (!new File(oldCertPath).exists()) {
      String errMsg = String.format("CA certificate '%s' does not exist", oldCertId);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errMsg);
    }

    try {
      // Validate new certificate's chain and expiry.
      List<X509Certificate> x509CACerts = getCertChain(customerId, name, newCertContents);
      CertificateHelper.verifyCertValidity(x509CACerts);
    } catch (CertificateException e) {
      log.error("Verification of certificate failed", e);
      throw new PlatformServiceException(BAD_REQUEST, e.getLocalizedMessage());
    }

    // Check if new cert and old cert contents are different.
    if (oldCert.getContents().equals(newCertContents)) {
      String errMsg = String.format("CA certificate '%s' has the same content already.", name);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    UUID newCertId = null;
    String newCertPath = null;
    boolean suppressErrors = false;
    char[] truststorePassword = getTruststorePassword();
    List<TrustStoreManager> trustStoreManagersToRollback = new ArrayList<>();
    try {
      List<X509Certificate> newX509CACerts = getCertChain(customerId, name, newCertContents);
      newCertId = UUID.randomUUID();
      newCertPath = getCustomCACertsPath(trustStoreHome, newCertId);
      CertificateHelper.writeCertBundleToCertPath(newX509CACerts, newCertPath);

      for (TrustStoreManager caStoreManager : trustStoreManagers) {
        caStoreManager.replaceCertificate(
            oldCertPath, newCertPath, name, trustStoreHome, truststorePassword, suppressErrors);
        trustStoreManagersToRollback.add(caStoreManager);
      }
      log.debug("Replaced on all runtime trust-stores");

      // Insert new CA certificate as active and deactivate old cert in DB.
      boolean activeCert = true;
      CustomCaCertificateInfo.create(
          customerId,
          newCertId,
          name,
          newCertPath,
          newX509CACerts.get(0).getNotBefore(),
          newX509CACerts.get(0).getNotAfter(),
          activeCert);
      // 2 certs will be active now, be sure to take the latest if certId is not
      // used in 'select' query.
      oldCert.deactivate();
    } catch (Exception e) {
      log.warn("Rolling back certificate update", e);
      // Rollback DB.
      CustomCaCertificateInfo newCert = CustomCaCertificateInfo.getOrGrunt(customerId, newCertId);
      oldCert.activate();
      newCert.delete();

      suppressErrors = true; // Rollback errors need not be reported to user.
      try {
        for (TrustStoreManager tSM : trustStoreManagersToRollback) {
          tSM.replaceCertificate(
              newCertPath,
              oldCertPath,
              name,
              trustStoreHome,
              getTruststorePassword(),
              suppressErrors);
        }
        log.debug("Rolled back all runtime trust-stores");

        // Delete files.
        if (newCertPath != null) purge(new File(newCertPath).getParentFile());
        log.debug("Rolled back local filesystem");
      } catch (KeyStoreException
          | CertificateException
          | NoSuchAlgorithmException
          | IOException ex) {
        log.warn("Cannot rollback refresh operation", ex);
        // We'd already written the cert bundle to newCertPath- delete it when refresh failed.
        try {
          purge(new File(newCertPath).getParentFile());
        } catch (IOException exc) {
          log.warn("Rolling back refresh failed. Error: ", exc);
        }
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getLocalizedMessage());
    }
    // Cleanup.
    boolean isDeleted = oldCert.delete();
    if (isDeleted) log.info("Replace old CA cert {} with new cert {}", oldCertId, newCertId);
    try {
      purge(new File(oldCertPath).getParentFile());
    } catch (IOException e) {
      log.error("Failed to cleanup file remnants. Error: {}", e.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getLocalizedMessage());
    }

    notifyListeners();
    log.debug("All trust-store listeners notified");

    return newCertId;
  }

  public boolean deleteCA(UUID customerId, UUID certId, String storagePath) {

    Customer.getOrBadRequest(customerId);
    CustomCaCertificateInfo cert = CustomCaCertificateInfo.getOrGrunt(customerId, certId);
    log.debug("Input validated");

    String trustStoreHome = getTruststoreHome(storagePath);
    String certPath = getCustomCACertsPath(trustStoreHome, certId);

    boolean deleted = false;
    boolean suppressErrors = false;
    char[] truststorePassword = getTruststorePassword();
    List<TrustStoreManager> trustStoreManagersToRollback = new ArrayList<>();

    try {
      for (TrustStoreManager caMgr : trustStoreManagers) {
        caMgr.remove(certPath, cert.getName(), trustStoreHome, truststorePassword, suppressErrors);
        trustStoreManagersToRollback.add(caMgr);
      }
      log.debug("Removed from all runtime trust-stores");

      cert.deactivate();
      log.debug("Inactivated CA certificate {}", certId);
    } catch (Exception e) {
      log.error("CA certificate delete is incomplete due to: ", e);
      // Rollback DB.
      CustomCaCertificateInfo origCert = CustomCaCertificateInfo.get(customerId, certId, false);
      // We need to ensure paths is not messed up in the custom cert table.
      cert.setContents(origCert.getContents());
      cert.activate();
      try {
        suppressErrors = true;
        for (TrustStoreManager tSM : trustStoreManagersToRollback) {
          tSM.addCertificate(
              certPath, cert.getName(), trustStoreHome, truststorePassword, suppressErrors);
        }
      } catch (Exception ex) {
        log.warn("Cannot complete rollback delete operation due to: ", ex);
      }
      String msg = String.format("Failed to delete custom CA cert: %s", e.getLocalizedMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
    }
    // Clean up.
    try {
      purge(new File(certPath).getParentFile());
    } catch (IOException e) {
      log.error("Failed to cleanup file remnants. Error: ", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getLocalizedMessage());
    }
    deleted = cert.delete();
    log.info("Deleted CA certificate {}", certId);

    notifyListeners();
    log.debug("All trust-store listeners notified");

    return deleted;
  }

  public List<CustomCaCertificateInfo> getAll() {
    boolean noContents = true;
    return CustomCaCertificateInfo.getAll(noContents);
  }

  public CustomCaCertificateInfo get(UUID customerId, UUID certId) {
    Customer.getOrBadRequest(customerId);
    return CustomCaCertificateInfo.getOrGrunt(customerId, certId);
  }

  // -------------- PEM CA trust-store specific methods ------------

  public List<Map<String, String>> getPemStoreConfig() {
    String storagePath = AppConfigHelper.getStoragePath();
    String trustStoreHome = getTruststoreHome(storagePath);
    List ybaTrustStoreConfig = new ArrayList<>();

    if (Files.exists(Paths.get(trustStoreHome))) {
      String pemStorePathStr = pemTrustStoreManager.getYbaTrustStorePath(trustStoreHome);
      Path pemStorePath = Paths.get(pemStorePathStr);
      if (Files.exists(pemStorePath)) {
        if (!pemTrustStoreManager.isTrustStoreEmpty(pemStorePathStr, getTruststorePassword())) {
          Map<String, String> trustStoreMap = new HashMap<>();
          trustStoreMap.put("path", pemStorePathStr);
          trustStoreMap.put("type", pemTrustStoreManager.getYbaTrustStoreType());
          ybaTrustStoreConfig.add(trustStoreMap);
        }
      }
    }

    log.debug("YBA's custom trust store config is {}", ybaTrustStoreConfig);
    return ybaTrustStoreConfig;
  }

  // -------------- PKCS12 CA trust-store specific methods ------------

  private KeyStore getYbaKeyStore() {
    String storagePath = AppConfigHelper.getStoragePath();
    String trustStoreHome = getTruststoreHome(storagePath);
    String pkcs12StorePathStr = pkcs12TrustStoreManager.getYbaTrustStorePath(trustStoreHome);
    KeyStore pkcs12Store =
        pkcs12TrustStoreManager.maybeGetTrustStore(pkcs12StorePathStr, getTruststorePassword());
    return pkcs12Store;
  }

  public KeyStore getYbaAndJavaKeyStore() {
    // Add YBA certs into this default keystore.
    KeyStore ybaJavaKeyStore = pkcs12TrustStoreManager.getJavaDefaultKeystore();

    try {
      KeyStore ybaKeyStore = getYbaKeyStore();
      if (ybaKeyStore != null) {
        Enumeration<String> aliases = ybaKeyStore.aliases();
        while (aliases.hasMoreElements()) {
          String alias = aliases.nextElement();
          ybaJavaKeyStore.setCertificateEntry(alias, ybaKeyStore.getCertificate(alias));
        }
      }
    } catch (KeyStoreException e) {
      log.error("Failed to get aliases from YBA's custom keystore", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getLocalizedMessage());
    }
    return ybaJavaKeyStore;
  }

  public Map<String, String> getJavaDefaultConfig() {
    return pkcs12TrustStoreManager.getJavaDefaultConfig();
  }

  // ---------------- helper methods ------------------

  private List<X509Certificate> getCertChain(UUID customerId, String name, String contents)
      throws CertificateException {
    if (customerId == null || StringUtils.isAnyBlank(name, contents)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "None of customerId, certificate-name, certificate-contents can be blank");
    }
    List<X509Certificate> x509CACerts = CertificateHelper.convertStringToX509CertList(contents);
    log.debug("Cert chain is {}", x509CACerts);
    return x509CACerts;
  }

  private CustomCaCertificateInfo validateAndGetCert(UUID certId, UUID customerId, String name) {
    CustomCaCertificateInfo cert = CustomCaCertificateInfo.getOrGrunt(customerId, certId);

    String certName = cert.getName();
    if (!certName.equals(name)) {
      String msg =
          String.format(
              "Certificate %s has name %s. It doesn't exist with name '%s'",
              certId, certName, name);
      throw new PlatformServiceException(BAD_REQUEST, msg);
    }
    return cert;
  }

  private String getCustomCACertsPath(String trustStoreHome, UUID certId) {
    return String.format("%s/%s/ca.crt", trustStoreHome, certId.toString());
  }

  private static String getTruststoreHome(String storagePath) {
    return String.format("%s/certs/trust-store", storagePath);
  }

  private void purge(File file) throws IOException {
    List<String> filesToRemove =
        file.isDirectory() ? FileUtils.listFiles(file) : Arrays.asList(file.getAbsolutePath());
    boolean isDeleted = org.apache.commons.io.FileUtils.deleteQuietly(file);
    if (isDeleted) {
      removeFromBackup(filesToRemove);
    }
  }

  private void removeFromBackup(List<String> files) {
    if (files == null) return;
    files.forEach(
        filePath -> {
          boolean isDeleted = FileData.deleteFileFromDB(filePath);
          log.debug(
              "File {}, status = {}", filePath, isDeleted ? "deleted" : "doesn't exist in backup");
        });
  }

  private char[] getTruststorePassword() {
    // TODO: remove hard coded password.
    return "global-truststore-password".toCharArray();
  }

  public boolean isEnabled() {
    return runtimeConfigFactory.globalRuntimeConf().getBoolean(CUSTOM_CA_STORE_ENABLED);
  }

  // ---------------- methods for CA store observers ---------------
  public void addListener(CustomTrustStoreListener listener) {
    trustStoreListeners.add(listener);
  }

  private void notifyListeners() {
    for (CustomTrustStoreListener listener : trustStoreListeners) {
      listener.truststoreUpdated();
    }
  }

  public boolean areCustomCAsPresent() {
    List<CustomCaCertificateInfo> customCAList = getAll();
    if (customCAList.isEmpty()) {
      log.debug("There are no custom CAs uploaded");
      return false;
    }
    log.info("There are {} custom CAs uploaded", customCAList.size());
    return true;
  }
}
