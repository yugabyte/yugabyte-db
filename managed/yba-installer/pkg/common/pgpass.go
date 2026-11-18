package common

import (
	"fmt"

	"github.com/spf13/viper"
)

// PgpassEntry is the .pgpass line for postgres.useExisting. It matches every database:
// yb_platform_backup.sh exports PGPASSFILE, so libpq skips ~/.pgpass, and the script also
// connects to the PA "ts" database, the "postgres" maintenance database (createdb) and the
// database named after the user (db_exists).
func PgpassEntry() string {
	return fmt.Sprintf("%s:%s:*:%s:%s",
		viper.GetString("postgres.useExisting.host"),
		viper.GetString("postgres.useExisting.port"),
		viper.GetString("postgres.useExisting.username"),
		viper.GetString("postgres.useExisting.password"),
	)
}
