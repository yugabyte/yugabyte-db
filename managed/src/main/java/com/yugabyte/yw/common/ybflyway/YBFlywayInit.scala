package com.yugabyte.yw.common.ybflyway

import java.io.FileNotFoundException

import javax.inject._
import org.flywaydb.core.Flyway
import org.flywaydb.core.api.MigrationInfo
import org.flywaydb.core.internal.util.jdbc.DriverDataSource
import org.flywaydb.play._
import play.api._
import play.core._

import scala.collection.JavaConverters._

/**
 * This class is mostly similar to org.flywaydb.play.PlayInitializer from the flyway-play plugin
 * Having to do this so that we can workaround the bugs in the plugin.
 *
 * https://github.com/flyway/flyway-play/issues/96
 * Note: All the workaround logic is in methods are implemented as companion methods in YBFlywayInit
 */
@Singleton
class YBFlywayInit @Inject()(
                                 appConfiguration: Configuration,
                                 environment: Environment,
                                 webCommands: WebCommands
                               ) {
  private val flywayConfigurations = {
    val configReader = new ConfigReader(appConfiguration, environment)
    configReader.getFlywayConfigurations
  }

  private val allDatabaseNames = flywayConfigurations.keys

  private val flywayPrefixToMigrationScript = "db/migration"

  private def migrationFileDirectoryExists(path: String): Boolean = {
    environment.resource(path) match {
      case Some(_) =>
        Logger.debug(s"Directory for migration files found. $path")
        true
      case None =>
        Logger.warn(s"Directory for migration files not found. $path")
        false
    }
  }

  private lazy val flyways: Map[String, Flyway] = {
    for {
      (dbName, flywayConfiguration) <- flywayConfigurations
      migrationFilesLocation = s"$flywayPrefixToMigrationScript/$dbName"
      if migrationFileDirectoryExists(migrationFilesLocation)
    } yield {
      val flyway = new Flyway
      val database = flywayConfiguration.database
      val dataSource = new DriverDataSource(
        getClass.getClassLoader,
        database.driver,
        database.url,
        database.user,
        database.password,
        null
      )
      flyway.setDataSource(dataSource)
      if (flywayConfiguration.locations.nonEmpty) {
        YBFlywayInit.ensureJavaFriendlyLocation(dbName,
          flywayConfiguration,
          migrationFilesLocation,
          flyway)
      } else {
        flyway.setLocations(migrationFilesLocation)
      }
      flywayConfiguration.encoding.foreach(flyway.setEncoding)
      flyway.setSchemas(flywayConfiguration.schemas: _*)
      flywayConfiguration.table.foreach(flyway.setTable)
      flywayConfiguration.placeholderReplacement.foreach(flyway.setPlaceholderReplacement)
      flyway.setPlaceholders(flywayConfiguration.placeholders.asJava)
      flywayConfiguration.placeholderPrefix.foreach(flyway.setPlaceholderPrefix)
      flywayConfiguration.placeholderSuffix.foreach(flyway.setPlaceholderSuffix)
      flywayConfiguration.sqlMigrationPrefix.foreach(flyway.setSqlMigrationPrefix)
      flywayConfiguration
        .repeatableSqlMigrationPrefix
        .foreach(flyway.setRepeatableSqlMigrationPrefix)
      flywayConfiguration.sqlMigrationSeparator.foreach(flyway.setSqlMigrationSeparator)
      flywayConfiguration.sqlMigrationSuffix.foreach(flyway.setSqlMigrationSuffix)
      flywayConfiguration.ignoreFutureMigrations.foreach(flyway.setIgnoreFutureMigrations)
      flywayConfiguration.validateOnMigrate.foreach(flyway.setValidateOnMigrate)
      flywayConfiguration.cleanOnValidationError.foreach(flyway.setCleanOnValidationError)
      flywayConfiguration.cleanDisabled.foreach(flyway.setCleanDisabled)
      flywayConfiguration.initOnMigrate.foreach(flyway.setBaselineOnMigrate)
      flywayConfiguration.outOfOrder.foreach(flyway.setOutOfOrder)

      YBFlywayInit.ybPatchAdditionalConfigurations(dbName, flyway, appConfiguration)

      dbName -> flyway
    }
  }

  private def migrationDescriptionToShow(dbName: String, migration: MigrationInfo): String = {
    val locations = flywayConfigurations(dbName).locations
    (if (locations.nonEmpty) {
      locations.map(location => environment
        .resourceAsStream(
          s"$flywayPrefixToMigrationScript/$dbName/$location/${migration.getScript}"))
        .find(resource => resource.nonEmpty).flatten
    } else {
      environment.resourceAsStream(s"$flywayPrefixToMigrationScript/$dbName/${migration.getScript}")
    }).map { in =>
      s"""|--- ${migration.getScript} ---
          |${FileUtils.readInputStreamToString(in)}""".stripMargin
    }.orElse {
      import scala.util.control.Exception._
      val code = for {
        script <- FileUtils.findJdbcMigrationFile(environment.rootPath, migration.getScript)
      } yield FileUtils.readFileToString(script)
      allCatch opt {
        environment.classLoader.loadClass(migration.getScript)
      } map { _ =>
        s"""|--- ${migration.getScript} ---
            |$code""".stripMargin
      }
    }.getOrElse(throw new FileNotFoundException(
      s"Migration file not found. ${migration.getScript}"))
  }

  private def checkState(dbName: String): Unit = {
    flyways.get(dbName).foreach { flyway =>
      val pendingMigrations = flyway.info().pending
      if (pendingMigrations.nonEmpty) {
        throw InvalidDatabaseRevision(
          dbName,
          pendingMigrations
            .map(migration => migrationDescriptionToShow(dbName, migration))
            .mkString("\n")
        )
      }

      if (flywayConfigurations(dbName).validateOnStart) {
        flyway.validate()
      }
    }
  }

  def onStart(): Unit = {
    val flywayWebCommand = new FlywayWebCommand(
      appConfiguration, environment, flywayPrefixToMigrationScript, flyways)
    webCommands.addHandler(flywayWebCommand)

    for (dbName <- allDatabaseNames) {
      if (environment.mode == Mode.Test || flywayConfigurations(dbName).auto) {
        migrateAutomatically(dbName)
      } else {
        checkState(dbName)
      }
    }
  }

  private def migrateAutomatically(dbName: String): Unit = {
    flyways.get(dbName).foreach { flyway =>
      flyway.migrate()
    }
  }

  val enabled: Boolean =
    !appConfiguration.getOptional[String]("flywayplugin").contains("disabled")

  if (enabled) {
    onStart()
  }

}

object YBFlywayInit {

  // Additional configuration not handled by the older plugin version
  private def ybPatchAdditionalConfigurations(dbName: String,
                                              flyway: Flyway,
                                              configuration: Configuration): Unit = {
    val subConfig = configuration.getOptional[Configuration](s"db.$dbName.migration")
      .getOrElse(Configuration.empty)
    subConfig.getOptional[Boolean]("ignoreMissingMigrations")
      .foreach(flyway.setIgnoreMissingMigrations)
  }

  private def ensureJavaFriendlyLocation(dbName: String,
                                         flywayConfiguration: FlywayConfiguration,
                                         migrationFilesLocation: String,
                                         flyway: Flyway): Unit = {
    val locations = flywayConfiguration
      .locations
      .map(location => s"$migrationFilesLocation/$location")
    if (dbName == "default") {
      val javaFriendlyLocations = flywayConfiguration
        .locations
        .map(location => s"${migrationFilesLocation}_/$location")
      flyway.setLocations(javaFriendlyLocations ++ locations: _*)
    } else {
      flyway.setLocations(locations: _*)
    }
  }
}
