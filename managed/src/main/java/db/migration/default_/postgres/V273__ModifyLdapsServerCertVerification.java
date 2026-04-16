// Copyright (c) YugaByte, Inc.

package db.migration.default_.postgres;

import java.sql.Connection;
import java.sql.ResultSet;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V273__ModifyLdapsServerCertVerification extends BaseJavaMigration {
  private final String LDAP_VERIFY_CERT = "yb.security.ldap.enforce_server_cert_verification";
  private final String LDAPS_CONF = "yb.security.ldap.enable_ldaps";
  private final String LDAP_TLS_CONF = "yb.security.ldap.enable_ldap_start_tls";

  @Override
  public void migrate(Context context) throws Exception {
    Connection connection = context.getConnection();
    String findQuery =
        String.format("SELECT path from runtime_config_entry where path='%s'", LDAP_VERIFY_CERT);
    ResultSet runtimeEntry = connection.createStatement().executeQuery(findQuery);
    if (runtimeEntry.next()) {
      String path = runtimeEntry.getString("path");
      log.debug("{} runtime config already exists. Not overriding", path);
    } else {
      String noVerify =
          String.format(
              "INSERT into runtime_config_entry(scope_uuid, path, value) VALUES "
                  + "('00000000-0000-0000-0000-000000000000', '%s', 'false')",
              LDAP_VERIFY_CERT);

      String sVal =
          String.format(
              "SELECT convert_from(value, 'UTF8') as value from runtime_config_entry "
                  + "where path='%s'",
              LDAPS_CONF);
      ResultSet ldapsConfig = connection.createStatement().executeQuery(sVal);
      if (ldapsConfig.next()) {
        String ldapsEnabled = ldapsConfig.getString("value");
        if (StringUtils.equals(ldapsEnabled, "\"true\"")
            || StringUtils.equals(ldapsEnabled, "true")) {
          log.warn("LDAPs is configured, disabling server cert verification with LDAPs");
          connection.createStatement().executeUpdate(noVerify);
        }
      } else {
        String tlsQ =
            String.format(
                "SELECT convert_from(value, 'UTF8') as value from runtime_config_entry where "
                    + "path='%s'",
                LDAP_TLS_CONF);
        ResultSet ldapTlsVal = connection.createStatement().executeQuery(tlsQ);
        if (ldapTlsVal.next()) {
          String ldapTlsEnabled = ldapTlsVal.getString("value");
          if (StringUtils.equals(ldapTlsEnabled, "\"true\"")
              || StringUtils.equals(ldapTlsEnabled, "true")) {
            log.warn("LDAP-TLS is configured, disabling server cert verification with LDAP-TLS");
            connection.createStatement().executeUpdate(noVerify);
          }
        } else {
          log.info(
              "LDAPs or LDAP-TLS is not configured, enforcing {} by default.", LDAP_VERIFY_CERT);
        }
      }
    }
  }
}
