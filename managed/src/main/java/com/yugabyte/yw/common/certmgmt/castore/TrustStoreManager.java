// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import java.io.FileInputStream;
import java.io.IOException;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;

public interface TrustStoreManager {
  default String getTrustStorePath(String trustStoreHome, String trustStoreFileName) {
    return String.format("%s/%s", trustStoreHome, trustStoreFileName);
  }

  default Certificate getX509Certificate(String certPath) throws CertificateException, IOException {
    try (FileInputStream certStream = new FileInputStream(certPath)) {
      CertificateFactory certificateFactory = CertificateFactory.getInstance("X.509");
      Certificate certificate = certificateFactory.generateCertificate(certStream);
      return certificate;
    }
  }

  boolean addCertificate(
      String certPath,
      String name,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws KeyStoreException, CertificateException, IOException;

  void remove(
      String certPath,
      String name,
      String trustStoreHome,
      char[] trustStorePassword,
      boolean suppressErrors)
      throws CertificateException, IOException, KeyStoreException;

  void replaceCertificate(
      String oldCertPath,
      String newCertPath,
      String name,
      String trustStoreHome,
      char[] truststorePassword,
      boolean suppressErrors)
      throws CertificateException, IOException, KeyStoreException, NoSuchAlgorithmException;
}
