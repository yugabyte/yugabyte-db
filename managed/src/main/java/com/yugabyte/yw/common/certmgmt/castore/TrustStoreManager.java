// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.certmgmt.castore;

import java.io.FileInputStream;
import java.io.IOException;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import lombok.ToString.Exclude;
import lombok.Value;
import org.apache.commons.lang3.StringUtils;

public interface TrustStoreManager {
  default String getTrustStorePath(String trustStoreHome, String trustStoreFileName) {
    return String.format("%s/%s", trustStoreHome, trustStoreFileName);
  }

  default List<Certificate> getX509Certificate(String certPath)
      throws CertificateException, IOException {
    List<Certificate> certificates = new ArrayList<>();
    try (FileInputStream certStream = new FileInputStream(certPath)) {
      CertificateFactory certificateFactory = CertificateFactory.getInstance("X.509");

      while (certStream.available() > 0) {
        Certificate certificate = certificateFactory.generateCertificate(certStream);
        certificates.add(certificate);
      }
      return certificates;
    }
  }

  void addCertificate(String certPath, String name, String trustStoreHome, boolean suppressErrors)
      throws KeyStoreException, CertificateException, IOException;

  void remove(String certPath, String name, String trustStoreHome, boolean suppressErrors)
      throws CertificateException, IOException, KeyStoreException;

  void replaceCertificate(
      String oldCertPath,
      String newCertPath,
      String name,
      String trustStoreHome,
      boolean suppressErrors)
      throws CertificateException, IOException, KeyStoreException, NoSuchAlgorithmException;

  TrustStoreInfo getYbaTrustStoreInfo(String trustStoreHome);

  @Value
  class TrustStoreInfo {
    String path;
    String type;
    @Exclude String password;

    public Map<String, String> toPlayConfig() {
      Map<String, String> config = new HashMap<>();
      config.put("path", path);
      config.put("type", type);
      if (StringUtils.isNotEmpty(password)) {
        config.put("password", password);
      }
      return config;
    }
  }
}
