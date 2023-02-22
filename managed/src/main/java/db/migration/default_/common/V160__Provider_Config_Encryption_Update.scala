// Copyright (c) YugaByte, Inc.

package db.migration.default_.common

import java.util.Map
import java.util.UUID
import com.yugabyte.yw.models.helpers.CommonUtils
import com.fasterxml.jackson.databind.ObjectMapper
import org.flywaydb.core.api.migration.{BaseJavaMigration, Context}

class V160__Provider_Config_Encryption_Update extends BaseJavaMigration {
  override def migrate(context: Context): Unit = {
    val connection = context.getConnection
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
