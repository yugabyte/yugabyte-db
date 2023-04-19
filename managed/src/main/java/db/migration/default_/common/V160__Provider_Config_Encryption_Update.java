// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.models.helpers.CommonUtils;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

public class V160__Provider_Config_Encryption_Update extends BaseJavaMigration {
  public void migrate(Context context) throws SQLException, JsonProcessingException {
    Connection connection = context.getConnection();
    String selectStmt = "SELECT uuid, config, code, customer_uuid FROM provider";
    ResultSet resultSet = connection.createStatement().executeQuery(selectStmt);

    ObjectMapper objectMapper = new ObjectMapper();
    while (resultSet.next()) {
      String providerUuid = resultSet.getString("uuid");
      String config = resultSet.getString("config");
      String code = resultSet.getString("code");
      UUID customerUuid = UUID.fromString(resultSet.getString("customer_uuid"));
      TypeReference<HashMap<String, String>> mapTypeReference =
          new TypeReference<HashMap<String, String>>() {};
      Map<String, String> configMap = objectMapper.readValue(config, mapTypeReference);
      Map<String, String> encryptedMap =
          CommonUtils.encryptProviderConfig(configMap, customerUuid, code);
      config = objectMapper.writeValueAsString(encryptedMap);
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
  }
}
