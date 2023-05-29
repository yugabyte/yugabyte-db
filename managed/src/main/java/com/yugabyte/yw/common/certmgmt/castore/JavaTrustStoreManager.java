// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.FileData;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.util.Collections;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class JavaTrustStoreManager implements TrustStoreManager {

  public static final String TRUSTSTORE_FILE_NAME = "ybJksCaCerts";

  /**
   * Creates a new trust store file (with the default trust store), if it does not exist and adds
   * the custom CA certificate to it.
   */
  public boolean addCertificate(
      String certPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws KeyStoreException, CertificateException, IOException, PlatformServiceException {

    log.debug("Trying to update YBA's Java truststore ...");
    // Get the existing trust bundle.
    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    log.debug("Updating truststore {}", trustStorePath);

    boolean doesTrustStoreExist = new File(trustStorePath).exists();
    KeyStore trustStore = null;
    if (!doesTrustStoreExist) {
      File trustStoreFile = new File(trustStorePath);
      trustStoreFile.createNewFile();
      log.debug("Created an empty YBA JKS trust-store");
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

    Certificate certificate = getX509Certificate(certPath);
    trustStore.setCertificateEntry(certAlias, certificate);
    // Update the trust store in file-system.
    saveTrustStore(trustStorePath, trustStore, trustStorePassword);
    log.debug("Truststore '{}' now has a certificate with alias '{}'", trustStorePath, certAlias);

    // Backup up YBA's JKS trust store in DB.
    FileData.addToBackup(Collections.singletonList(trustStorePath));

    log.info("Custom CA certificate added in YBA's JKS trust-store");
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
    log.debug("Trying to replace cert {} in YBA's Java truststore {}", certAlias, trustStorePath);
    KeyStore trustStore = getTrustStore(trustStorePath, trustStorePassword, false);
    if (trustStore == null) {
      String errMsg = "Truststore cannot be null";
      log.error(errMsg);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errMsg);
    }

    // Check if such an alias already exists.
    if (!trustStore.containsAlias(certAlias) && !suppressErrors) {
      String errMsg = String.format("Cert by name '%s' does not exist to update", certAlias);
      log.error(errMsg);
      // Purge newCertPath which got created.
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    // Update the trust store.
    Certificate newCertificate = getX509Certificate(newCertPath);
    trustStore.setCertificateEntry(certAlias, newCertificate);
    saveTrustStore(trustStorePath, trustStore, trustStorePassword);

    // Backup up YBA's JKS trust store in DB.
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
      String msg = String.format("Failed to get trust store. Error %s", e.getLocalizedMessage());
      log.error(msg, e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
    }
  }

  public void remove(
      String certPath,
      String certAlias,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws KeyStoreException {
    log.info("Removing cert {} from YBA's JKS truststore ...", certAlias);

    String trustStorePath = getTrustStorePath(trustStoreHome, TRUSTSTORE_FILE_NAME);
    KeyStore trustStore = getTrustStore(trustStorePath, trustStorePassword, false);

    // Check if such an alias already exists.
    if (!trustStore.containsAlias(certAlias) && !suppressErrors) {
      String errMsg = String.format("CA certificate '%s' does not exist to delete", certAlias);
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    // Delete from the trust store.
    if (trustStore.containsAlias(certAlias)) {
      trustStore.deleteEntry(certAlias);
    }
    saveTrustStore(trustStorePath, trustStore, trustStorePassword);
    log.debug("Truststore '{}' now does not have a CA certificate '{}'", trustStorePath, certAlias);

    log.info("Custom CA certs deleted in YBA's Java truststore");
  }
}
