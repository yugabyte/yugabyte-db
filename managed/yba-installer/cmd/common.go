package cmd

import (
	log "github.com/yugabyte/yugabyte-db/managed/yba-installer/pkg/logging"
)

func migratePgToYbdbOrFatal() {
	log.Info("Migrating data from Postgres to YBDB.")
	/*
		PgService, ok := services[PostgresServiceName].(Postgres)
		if !ok {
			log.Fatal("Could not cast service to Postgres")
		}
		/*
			YbdbService, ok := services[YbdbServiceName].(Ybdb)
			if !ok {
				log.Fatal("Could not cast service to Ybdb.")
			}

			backupLocation := filepath.Join(common.GetBaseInstall(), "data", "yugaware_pg_backup")
			if _, err := os.Stat(backupLocation); !errors.Is(err, os.ErrNotExist) {
				os.Remove(backupLocation)
			}

			log.Info("Creating Postgres backup for YBDB")
			//Pg create Yugaware backup
			PgService.CreateYugawareBackup(backupLocation)

			log.Info("Installing YBDB")
			if err := YbdbService.Install(); err != nil {
				log.Fatal("Could not install YBDB: " + err.Error())
			}

			log.Info("Restoring postgres data to YBDB")
			//Ybdb restore Yugaware backup
			YbdbService.RestoreBackup(backupLocation)

			//Uninstall PgService
			PgService.Uninstall(true)
			os.Remove(backupLocation)
	*/
}

func migrateYbdbToPgOrFatal() {
	log.Info("Migrating data from YBDB to Postgres.")
	/*
		pgService, ok := services[PostgresServiceName].(Postgres)
		if !ok {
			log.Fatal("Could not cast service to Postgres")
		}
		/*
			ybdbService, ok := services[YbdbServiceName].(Ybdb)
			if !ok {
				log.Fatal("Could not cast service to Ybdb.")
			}

			log.Info("Installing Postgres")
			if err := pgService.Install(); err != nil {
				log.Fatal("Could not install Postgres: " + err.Error())
			}

			backupLocation := filepath.Join(common.GetBaseInstall(), "data", "yugaware_ybdb_backup")

			//Workaround to create a pg_dump backup from ybdb when
			//ybdb.install.read_committed_isolation is enabled.
			//See issue - https://github.com/yugabyte/yugabyte-db/issues/12494
			//Stop YBDB service -> change isolation level to Snapshot -> pg_dump -> Uninstall YBDB.
			log.Info("Creating Ybdb backup for PostgreSQL")
			ybdbService.Stop()
			viper.Set("ybdb.install.read_committed_isolation", false)
			config.GenerateTemplate(ybdbService)
			ybdbService.Start()
			ybdbService.WaitForYbdbReadyOrFatal(5)
			//Pg create Yugaware backup
			ybdbService.CreateBackupUsingPgDump(pgService.PgBin+"/pg_dump", backupLocation)

			log.Info("Restoring YBDB data to Postgres")
			//Postgres restore Yugaware backup
			pgService.RestoreBackup(backupLocation)

			log.Info("Uninstalling Ybdb")
			//Uninstall Ybdb
			ybdbService.Uninstall(true)
			os.Remove(backupLocation)
	*/
}
