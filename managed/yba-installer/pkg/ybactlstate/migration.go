package ybactlstate

const defaultMigratorValue = -1

func handleMigration(state *State) error {
	for state._internalFields.SchemaVersion < schemaVersion {
		nextSchema := state._internalFields.SchemaVersion + 1
		migrate := getMigrationHandler(nextSchema)
		if err := migrate(state); err != nil {
			return err
		}
		state._internalFields.SchemaVersion = nextSchema
	}
	return nil
}

type migrator func(state *State) error

// Migrate on default is a no-op, mainly assuming that the default values given to struct fields
// are sufficient.
func defaultMigrate(state *State) error {
	return nil
}

var migrations map[int]migrator = map[int]migrator{
	defaultMigratorValue: defaultMigrate,
}

func getMigrationHandler(toSchema int) migrator {
	m, ok := migrations[toSchema]
	if !ok {
		m = migrations[defaultMigratorValue]
	}
	return m
}
