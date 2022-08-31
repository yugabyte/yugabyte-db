// Copyright (c) YugaByte, Inc.

package db.migration.default.postgres

import java.sql.Connection
import java.util.Map
import java.util.UUID

import org.flywaydb.core.api.migration.jdbc.JdbcMigration
import play.api.libs.json._
import com.yugabyte.yw.models.helpers.CommonUtils
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil
import com.yugabyte.yw.common.kms.util.KeyProvider
import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.node.ObjectNode

class V195__DB_Encryption_Update extends JdbcMigration {
  override def migrate(connection: Connection): Unit = {
    // Install pgcrypto
    var extensionSet = connection.createStatement.executeQuery("select extname from pg_extension;")
    var pgcryptoExists = false
    while(extensionSet.next()) {
      val extensionName = extensionSet.getString("extname")
      if (extensionName == "pgcrypto") {
        pgcryptoExists = true
      }
    }
    if (!pgcryptoExists) {
      var versionResult = connection.createStatement.executeQuery("SHOW server_version;")
      var version = 0
      if (versionResult.next()) {
        version = versionResult.getString(1).split('.')(0).toInt
      }
      // gen_random_uuid is included in core functions from pg_13. Prior to that it was included
      // as part of pg_crypto extension.
      // We also create the function `gen_random_uuid` as part of this migration
      // https://github.com/yugabyte/yugabyte-db/blob/master/managed/src/main/resources/db/migration/default/postgres/V93__Fill_Alert_Definition_Group_Table.sql#L34,
      // dropping it won't work for version >= 13. Hence, renaming for the purpose of installing
      // pgcypto extension.
      if (version < 13) {
        connection.createStatement.execute("DROP FUNCTION IF EXISTS gen_random_uuid();")
      } else {
        connection.createStatement.execute("ALTER function " +
          "public.gen_random_uuid rename to gen_random_uuid_stale;")
      }
      connection.createStatement.execute("CREATE EXTENSION IF NOT EXISTS pgcrypto;")
    }

    // Migrate the customer_config table to use new encryption
    connection.createStatement.executeUpdate("ALTER TABLE customer_config ALTER COLUMN data TYPE" +
      " bytea USING pgp_sym_encrypt(data::text, 'customer_config::data')")

    // Migrate the provider table to use new encryption
    var selectStmt = "SELECT uuid, config, code, customer_uuid FROM provider"
    var resultSet = connection.createStatement().executeQuery(selectStmt)
    while(resultSet.next()) {
      val providerUuid = resultSet.getString("uuid")
      var config = resultSet.getString("config")
      val code = resultSet.getString("code")
      val customerUuid = UUID.fromString(resultSet.getString("customer_uuid"))
      val objectMapper = new ObjectMapper()
      val configMap = objectMapper.readValue(config, classOf[Map[String, String]])
      val decryptedMap = CommonUtils.decryptProviderConfig(configMap, customerUuid, code);
      config = objectMapper.writeValueAsString(decryptedMap)
      connection.createStatement().execute(s"UPDATE provider SET config = '${config}'"+
        s" WHERE uuid = '$providerUuid'")
    }
    connection.createStatement.executeUpdate("ALTER TABLE provider ALTER COLUMN config TYPE" +
      " bytea USING pgp_sym_encrypt(config::text, 'provider::config')")

    // Migrate the kms_config table to use new encryption
    selectStmt = "SELECT config_uuid, customer_uuid, key_provider, auth_config FROM kms_config"
    resultSet = connection.createStatement().executeQuery(selectStmt)
    while(resultSet.next()) {
      val configUuid = resultSet.getString("config_uuid")
      var config = resultSet.getString("auth_config")
      val keyProvider = KeyProvider.valueOf(resultSet.getString("key_provider"))
      val customerUuid = UUID.fromString(resultSet.getString("customer_uuid"))
      val objectMapper = new ObjectMapper()
      val configMap = objectMapper.readValue(config, classOf[ObjectNode])
      val decryptedMap = EncryptionAtRestUtil.unmaskConfigData(customerUuid, configMap, keyProvider)
      config = objectMapper.writeValueAsString(decryptedMap)
      connection.createStatement().execute(s"UPDATE kms_config SET auth_config = '${config}'"+
        s" WHERE config_uuid = '$configUuid'")
    }
    connection.createStatement.executeUpdate("ALTER TABLE kms_config ALTER COLUMN auth_config " +
      "TYPE bytea USING pgp_sym_encrypt(auth_config::text, 'kms_config::auth_config')")
  }
}
