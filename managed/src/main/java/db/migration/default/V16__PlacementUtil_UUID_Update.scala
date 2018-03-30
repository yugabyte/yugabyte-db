// Copyright (c) YugaByte, Inc.

package db.migration.default

import java.sql.Connection
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import com.yugabyte.yw.models.Universe.UniverseUpdater;

import org.flywaydb.core.api.migration.jdbc.JdbcMigration

class V16__PlacementUtil_UUID_Update extends JdbcMigration {
  class PlacementUuidUniverseUpdater extends UniverseUpdater {
    override def run(universe : Universe) = {
      val udt = universe.getUniverseDetails()
      val nds = udt.nodeDetailsSet.iterator()
      while ( nds.hasNext() ) {
        val nodeDetails = nds.next()
        if ( nodeDetails.placementUuid == null ) {
          nodeDetails.placementUuid = udt.getPrimaryCluster().uuid
        }
      }
    }
  }
  
  val updater = new PlacementUuidUniverseUpdater()
  
  override def migrate(connection: Connection) = {
    val statement = connection.createStatement()
    val resultSet = statement.executeQuery("SELECT universe_uuid FROM universe")
    while ( resultSet.next() ) {
      val str = resultSet.getString("universe_uuid")
      val universeUuid = UUID.fromString(str)
      Universe.saveDetails(universeUuid, updater)
    }
  }
}
