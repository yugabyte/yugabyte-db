// Copyright (c) YugaByte, Inc.

package db.migration.default_.common

import org.flywaydb.core.api.migration.{BaseJavaMigration, Context}

import play.api.libs.json._

class V216__Alter_Architecture_Type extends BaseJavaMigration {

  /**
     * Utility method to recursively apply a modification function to a json value and all its
     * elements. Modification function is applied top-down (i.e. applied to parents before children).
     *
     * @param json     the json value
     * @param path     the location of the current value inside the root json object
     * @param modifier the function to be applied to every json value, takes the current path as the
     *                 second argument to decide if/how to modify the current value.
     * @return the modified json value
     */
    def processJson(json: JsValue, path: Array[Any] = Array())
                   (implicit modifier: (JsValue, Array[Any]) => JsValue): JsValue = {
      modifier(json, path) match {
        case JsObject(underlying) =>
          JsObject(underlying.map(kv => kv._1 -> processJson(kv._2, path :+ kv._1)))
        case JsArray(value) =>
          JsArray(value.zipWithIndex.map(vi => processJson(vi._1, path :+ vi._2)))
        case jsval => jsval
      }
    }

  def updateArchType(json: JsValue, path: Array[Any]): JsValue = {
    path match {
      case Array( _ , "packages", _ : Integer) if(json \ "arch").isInstanceOf[JsDefined] =>
        val arch = (json \ "arch").get.as[String]
        var newArch = arch
        if(arch.equals("arm64")) {
          newArch = "aarch64"
        }
        json.as[JsObject] - "arch" + ("arch" -> JsString(newArch))
      case _ =>
        json
    }
  }

  override def migrate(context: Context): Unit = {
    val connection = context.getConnection

    // Update architecture of old provider's region
    val selectRegionStmt = "SELECT uuid, details FROM region"
    val resultRegion = connection.createStatement().executeQuery(selectRegionStmt)

    while(resultRegion.next()) {
      val uuid = resultRegion.getString("uuid")
      val regDetails = resultRegion.getString("details")
      if(regDetails != null) {
        val regionDetails = Json.parse(regDetails)
        var regionArch = regionDetails \ "arch"

        if(regionArch != null && regionArch.isInstanceOf[JsDefined]
            && regionArch.getOrElse(JsString("")).equals(JsString("arm64"))) {
          val newRegionDetails = regionDetails.as[JsObject] + ("arch" -> JsString("aarch64"))
          val updateRegionStmt = connection.prepareStatement(
            "UPDATE region SET details = ?::json_alias WHERE uuid = ?::uuid")
          updateRegionStmt.setString(1, Json.stringify(Json.toJson(newRegionDetails)))
          updateRegionStmt.setString(2, uuid)
          updateRegionStmt.execute()
        }
      }
    }


    // Update architecture of yugabyte-db releases
    val softwareConfigSelectStmt = "SELECT value FROM yugaware_property where name = 'SoftwareReleases'"
    val softwareConfig = connection.createStatement().executeQuery(softwareConfigSelectStmt)
    while(softwareConfig.next()) {
      val configValue = Json.parse(softwareConfig.getString("value"))
      val newConfigValue = processJson(configValue)(updateArchType).toString()
      connection.createStatement.execute(s"UPDATE yugaware_property SET value = " +
        s"'$newConfigValue' WHERE name = 'SoftwareReleases'")
    }


    // Remove unsed arm64 package details
    val ybcConfigSelectStmt = "SELECT value FROM yugaware_property where name = 'YbcSoftwareReleases'"
    val ybcConfig = connection.createStatement().executeQuery(ybcConfigSelectStmt)
    while(ybcConfig.next()) {
      val configValue = Json.parse(ybcConfig.getString("value"))
      val newConfigValue = processJson(configValue)(updateArchType).toString()
      connection.createStatement.execute(s"UPDATE yugaware_property SET value = " +
        s"'$newConfigValue' WHERE name = 'YbcSoftwareReleases'")
    }
  }
}
