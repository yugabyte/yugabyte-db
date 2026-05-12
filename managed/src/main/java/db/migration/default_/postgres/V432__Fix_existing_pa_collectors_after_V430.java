package db.migration.default_.postgres;

import io.ebean.DB;
import io.ebean.Transaction;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

public class V432__Fix_existing_pa_collectors_after_V430 extends BaseJavaMigration {

  @Override
  public void migrate(Context context) throws Exception {
    Connection connection = context.getConnection();
    String selectUuids = "SELECT uuid FROM pa_collector";
    ResultSet resultSet = connection.createStatement().executeQuery(selectUuids);
    while (resultSet.next()) {
      String paCollectorUuid = resultSet.getString("uuid");
      try (Transaction transaction = DB.beginTransaction()) {
        DB.sqlQuery(
                "SELECT "
                    + "pgp_sym_decrypt(api_token, 'pa_collector::api_token'), "
                    + "pgp_sym_decrypt(pa_api_token, 'pa_collector::pa_api_token') "
                    + "FROM pa_collector "
                    + "WHERE uuid = '"
                    + paCollectorUuid
                    + "'::uuid")
            .findList();
      } catch (Exception e) {
        // In case exception happens - this means that either api_token or pa_api_token are
        // encrypted using old key. Need to decrypt and encrypt with the new key
        PreparedStatement statement =
            connection.prepareStatement(
                "UPDATE pa_collector SET "
                    + "api_token = pgp_sym_encrypt("
                    + "pgp_sym_decrypt(api_token, 'troubleshooting_platform::api_token'), "
                    + "'pa_collector::api_token'), "
                    + "pa_api_token = pgp_sym_encrypt("
                    + "pgp_sym_decrypt(pa_api_token, 'troubleshooting_platform::tp_api_token'), "
                    + "'pa_collector::pa_api_token') "
                    + "WHERE uuid = ?::uuid");
        statement.setString(1, paCollectorUuid);
        statement.execute();
      }
    }
  }
}
