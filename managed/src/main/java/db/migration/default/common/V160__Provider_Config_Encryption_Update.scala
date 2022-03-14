// Copyright (c) YugaByte, Inc.

package db.migration.default.common

import java.sql.Connection
import java.util.Map
import java.util.UUID

import org.flywaydb.core.api.migration.jdbc.JdbcMigration
import play.api.libs.json._
import com.yugabyte.yw.models.helpers.CommonUtils
import com.fasterxml.jackson.databind.ObjectMapper

class V160__Provider_Config_Encryption_Update extends JdbcMigration {
  override def migrate(connection: Connection): Unit = {
    val selectStmt = "SELECT uuid, config, code, customer_uuid FROM provider"
    val resultSet = connection.createStatement().executeQuery(selectStmt)

    while(resultSet.next()) {
      val providerUuid = resultSet.getString("uuid")
      var config = resultSet.getString("config")
      val code = resultSet.getString("code")
      val customerUuid = UUID.fromString(resultSet.getString("customer_uuid"))
      val objectMapper = new ObjectMapper()
      val configMap = objectMapper.readValue(config, classOf[Map[String, String]])
      val encryptedMap = CommonUtils.encryptProviderConfig(configMap, customerUuid, code)
      config = objectMapper.writeValueAsString(encryptedMap)
      connection.createStatement().execute(s"UPDATE provider SET config = '${config}'"+
        s" WHERE uuid = '$providerUuid'")
    }
  }
}
