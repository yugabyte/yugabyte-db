// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.CustomCaCertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.FileData;
import java.io.File;
import java.io.IOException;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class CustomCAStoreManager {

  private final List<TrustStoreManager> trustStoreManagers = new ArrayList<>();

  @Inject
  public CustomCAStoreManager(
      JavaTrustStoreManager javaTrustStoreManager,
      SystemTrustStoreManager systemTrustStoreManager) {
    trustStoreManagers.add(javaTrustStoreManager);
    trustStoreManagers.add(systemTrustStoreManager);
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

    } catch (KeyStoreException | CertificateException | IOException | PlatformServiceException e) {
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
        log.warn("Cannot rollback add CA certificate due to {}", ex.getMessage());
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getLocalizedMessage());
    }
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
    } catch (KeyStoreException
        | NoSuchAlgorithmException
        | CertificateException
        | IOException
        | PlatformServiceException e) {
      log.warn("Rolling back cert refresh because of - {}", e.getMessage());
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
        purge(new File(newCertPath).getParentFile());
        log.debug("Rolled back local filesystem");
      } catch (KeyStoreException
          | CertificateException
          | NoSuchAlgorithmException
          | IOException ex) {
        log.warn("Cannot rollback refresh operation due to '{}'", ex.getMessage());
        // We'd already written the cert bundle to newCertPath- delete it when refresh failed.
        try {
          purge(new File(newCertPath).getParentFile());
        } catch (IOException exc) {
          log.warn("Rolling back refresh failed. Error: {}", exc.getLocalizedMessage());
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
    return newCertId;
  }

  public boolean deleteCA(UUID customerId, UUID certId, String storagePath) {

    Customer.getOrBadRequest(customerId);
    CustomCaCertificateInfo cert = CustomCaCertificateInfo.getOrGrunt(customerId, certId);
    log.debug("Input validated");

    String trustStoreHome = getTruststoreHome(storagePath);
    String certPath = getCustomCACertsPath(trustStoreHome, certId);
    boolean deleted = false;
    boolean marked = false;
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
      marked = true;
      log.debug("Inactivated CA certificate {}", certId);
    } catch (KeyStoreException | CertificateException | IOException e) {
      log.error("CA certificate delete is incomplete due to - {}", e.getMessage());
      // Rollback DB.
      if (cert == null) log.warn("CA certificate {} already deleted, cannot rollback", certId);
      cert.activate();
      try {
        suppressErrors = true;
        for (TrustStoreManager tSM : trustStoreManagersToRollback) {
          tSM.addCertificate(
              certPath, cert.getName(), trustStoreHome, truststorePassword, suppressErrors);
        }
      } catch (Exception ex) {
        log.warn("Cannot complete rollback delete operation due to {}", ex.getMessage());
      }
      String msg = String.format("Failed to delete custom CA cert: %s", e.getLocalizedMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
    }
    // Clean up.
    if (marked) {
      try {
        purge(new File(certPath).getParentFile());
      } catch (IOException e) {
        log.error("Failed to cleanup file remnants. Error: {}", e.getMessage());
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getLocalizedMessage());
      }
      deleted = cert.delete();
      log.info("Deleted CA certificate {}", certId);
    }
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
}
