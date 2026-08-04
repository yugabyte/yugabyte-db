package db.migration.default_.postgres;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;

import java.io.UnsupportedEncodingException;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V270__Ldap_SA_DN extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws SQLException, UnsupportedEncodingException {
    Connection connection = context.getConnection();

    String prefix = getValue(connection, "yb.security.ldap.ldap_dn_prefix");
    String username = getValue(connection, "yb.security.ldap.ldap_service_account_username");
    String baseDn = getValue(connection, "yb.security.ldap.ldap_basedn");
    if (!StringUtils.isEmpty(prefix)
        && !StringUtils.isEmpty(username)
        && !StringUtils.isEmpty(baseDn)) {
      String fullDn = prefix + username + "," + baseDn;
      upsertValue(connection, "yb.security.ldap.ldap_service_account_distinguished_name", fullDn);
    }
  }

  public static String getValue(Connection connection, String path) throws SQLException {
    ResultSet set =
        connection
            .createStatement()
            .executeQuery("SELECT value FROM runtime_config_entry where path='" + path + "'");
    String value = "";
    if (set.next()) {
      value = new String(set.getBytes("value"));
      if (value.charAt(0) == '"') {
        value = value.substring(1);
      }
      if (value.charAt(value.length() - 1) == '"') {
        value = value.substring(0, value.length() - 1);
      }
    }
    return value;
  }

  public static void upsertValue(Connection connection, String path, String value)
      throws SQLException, UnsupportedEncodingException {
    String statement = "";
    PreparedStatement preparedStatement;
    if (connection
        .createStatement()
        .executeQuery("SELECT value FROM runtime_config_entry where path='" + path + "'")
        .next()) {
      statement = "UPDATE runtime_config_entry SET value = ? WHERE path = ? ";
      preparedStatement = connection.prepareStatement(statement);
      preparedStatement.setBytes(1, value.getBytes(StandardCharsets.UTF_8));
      preparedStatement.setString(2, path);
    } else {
      statement = "INSERT INTO runtime_config_entry VALUES ( ? , ? , ? )";
      preparedStatement = connection.prepareStatement(statement);
      preparedStatement.setObject(1, GLOBAL_SCOPE_UUID);
      preparedStatement.setString(2, path);
      preparedStatement.setBytes(3, value.getBytes(StandardCharsets.UTF_8));
    }
    preparedStatement.executeUpdate();
  }
}
