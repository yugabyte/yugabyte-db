// Copyright (c) YugaByte, Inc.

package db.migration.default_.postgres;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

public class V195__DB_Encryption_Update extends BaseJavaMigration {
  @Override
  public void migrate(Context context) throws SQLException, JsonProcessingException {
    Connection connection = context.getConnection();
    // Install pgcrypto
    ResultSet extensionSet =
        connection.createStatement().executeQuery("select extname from pg_extension;");
    boolean pgcryptoExists = false;
    while (extensionSet.next()) {
      String extensionName = extensionSet.getString("extname");
      if (Objects.equals(extensionName, "pgcrypto")) {
        pgcryptoExists = true;
      }
    }
    if (!pgcryptoExists) {
      ResultSet versionResult = connection.createStatement().executeQuery("SHOW server_version;");
      int version = 0;
      if (versionResult.next()) {
        version = Integer.parseInt(versionResult.getString(1).split("\\.")[0]);
      }
      // gen_random_uuid is included in core functions from pg_13. Prior to that it was included
      // as part of pg_crypto extension.
      // We also create the function `gen_random_uuid` as part of this migration
      // https://github.com/yugabyte/yugabyte-db/blob/master/managed/src/main/resources/db/migration/default/postgres/V93__Fill_Alert_Definition_Group_Table.sql#L34,
      // dropping it won't work for version >= 13. Hence, renaming for the purpose of installing
      // pgcypto extension.
      if (version < 13) {
        connection.createStatement().execute("DROP FUNCTION IF EXISTS gen_random_uuid();");
      } else {
        connection
            .createStatement()
            .execute("ALTER function " + "public.gen_random_uuid rename to gen_random_uuid_stale;");
      }
      connection.createStatement().execute("CREATE EXTENSION IF NOT EXISTS pgcrypto;");
    }

    // Migrate the customer_config table to use new encryption
    connection
        .createStatement()
        .executeUpdate(
            "ALTER TABLE customer_config ALTER COLUMN data TYPE"
                + " bytea USING pgp_sym_encrypt(data::text, 'customer_config::data')");

    ObjectMapper objectMapper = new ObjectMapper();

    // Migrate the provider table to use new encryption
    String selectStmt = "SELECT uuid, config, code, customer_uuid FROM provider";
    ResultSet resultSet = connection.createStatement().executeQuery(selectStmt);
    while (resultSet.next()) {
      String providerUuid = resultSet.getString("uuid");
      String config = resultSet.getString("config");
      String code = resultSet.getString("code");
      UUID customerUuid = UUID.fromString(resultSet.getString("customer_uuid"));
      TypeReference<HashMap<String, String>> mapTypeReference =
          new TypeReference<HashMap<String, String>>() {};
      Map<String, String> configMap = objectMapper.readValue(config, mapTypeReference);
      Map<String, String> decryptedMap =
          CommonUtils.decryptProviderConfig(configMap, customerUuid, code);
      config = objectMapper.writeValueAsString(decryptedMap);
      connection
          .createStatement()
          .execute(
              "UPDATE provider SET config = '"
                  + config
                  + "'"
                  + " WHERE uuid = '"
                  + providerUuid
                  + "'");
    }
    connection
        .createStatement()
        .executeUpdate(
            "ALTER TABLE provider ALTER COLUMN config TYPE"
                + " bytea USING pgp_sym_encrypt(config::text, 'provider::config')");

    // Migrate the kms_config table to use new encryption
    selectStmt = "SELECT config_uuid, customer_uuid, key_provider, auth_config FROM kms_config";
    resultSet = connection.createStatement().executeQuery(selectStmt);
    while (resultSet.next()) {
      String configUuid = resultSet.getString("config_uuid");
      String config = resultSet.getString("auth_config");
      KeyProvider keyProvider = KeyProvider.valueOf(resultSet.getString("key_provider"));
      UUID customerUuid = UUID.fromString(resultSet.getString("customer_uuid"));
      ObjectNode configMap = objectMapper.readValue(config, ObjectNode.class);
      JsonNode decryptedMap =
          EncryptionAtRestUtil.unmaskConfigData(customerUuid, configMap, keyProvider);
      config = objectMapper.writeValueAsString(decryptedMap);
      connection
          .createStatement()
          .execute(
              "UPDATE kms_config SET auth_config = '"
                  + config
                  + "'"
                  + " WHERE config_uuid = '"
                  + configUuid
                  + "'");
    }
    connection
        .createStatement()
        .executeUpdate(
            "ALTER TABLE kms_config ALTER COLUMN auth_config "
                + "TYPE bytea USING pgp_sym_encrypt(auth_config::text, 'kms_config::auth_config')");
  }
}
