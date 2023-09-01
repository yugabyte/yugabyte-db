import React, { FC } from "react";
import { Box, Breadcrumbs, Link, makeStyles, MenuItem, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBDropdown } from "@app/components";
import { MigrationList } from "./MigrationList";
import TriangleDownIcon from "@app/assets/caret-down.svg";
import { MigrationDetails } from "./MigrationDetails";
import { MigrationPhase, MigrationStep, migrationSteps } from "./migration";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[600],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "start",
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  dividerHorizontal: {
    width: "100%",
    marginTop: theme.spacing(1.5),
    marginBottom: theme.spacing(1.5),
  },
  heading: {
    marginBottom: theme.spacing(5),
  },
  link: {
    "&:link, &:focus, &:active, &:visited, &:hover": {
      textDecoration: "none",
      color: theme.palette.text.primary,
    },
  },
  dropdown: {
    cursor: "pointer",
    marginRight: theme.spacing(1),
    display: "flex",
    flexDirection: "column",
  },
  dropdownContent: {
    color: "black",
  },
  dropdownHeader: {
    fontWeight: 500,
    color: theme.palette.grey[500],
    marginLeft: theme.spacing(2),
    marginRight: theme.spacing(2),
    fontSize: "11.5px",
    textTransform: "uppercase",
  },
}));

const migrationDataList = [
  {
    migration_uuid: "cb1cdd55-3a91-11ee-89b8-42010a9601e6",
    migration_name: "Migration Name1",
    migration_phase: 0,
    invocation_sequence: 0,
    complexity: "",
    source_db: "Oracle 18c",
    database_name: "database1",
    schema_name: "yugabyted",
    status: "In progress",
    invocation_timestamp: "11/07/2022, 09:55",
  },
  {
    migration_uuid: "231cdd15-3a91-11ee-89b8-42010a9601e4",
    migration_name: "Migration Name2",
    migration_phase: 1,
    invocation_sequence: 1,
    source_db: "PostgreSQL 13.3",
    complexity: "Easy",
    database_name: "database2",
    schema_name: "yugabyted",
    status: "In progress",
    invocation_timestamp: "11/07/2022, 09:55",
  },
  {
    migration_uuid: "231cdd15-3a91-11ee-89b8-42010a9601e4",
    migration_name: "Migration Name3",
    migration_phase: 2,
    invocation_sequence: 1,
    complexity: "Medium",
    source_db: "MySQL 8.0.25",
    database_name: "database3",
    schema_name: "yugabyted",
    status: "In progress",
    invocation_timestamp: "11/07/2022, 09:55",
  },
  {
    migration_uuid: "231cdd15-3a91-11ee-89b8-42010a9601e4",
    migration_name: "Migration Name4",
    migration_phase: 3,
    invocation_sequence: 1,
    complexity: "Medium",
    source_db: "MySQL 8.0.25",
    database_name: "database4",
    schema_name: "sales",
    status: "In progress",
    invocation_timestamp: "11/07/2022, 09:55",
  },
  {
    migration_uuid: "231cdd15-3a91-11ee-89b8-42010a9601e4",
    migration_name: "Migration Name5",
    migration_phase: 4,
    invocation_sequence: 1,
    complexity: "Medium",
    source_db: "MySQL 8.0.25",
    database_name: "database5",
    schema_name: "yugabyted",
    status: "In progress",
    invocation_timestamp: "11/07/2022, 09:55",
  },
  {
    migration_uuid: "de3cdd86-3a91-11ee-89b8-42010a9601de",
    migration_name: "Migration Name6",
    migration_phase: 5,
    invocation_sequence: 2,
    complexity: "Hard",
    source_db: "Oracle 19c",
    database_name: "database6",
    schema_name: "engineering",
    status: "Completed",
    invocation_timestamp: "11/07/2022, 09:55",
  },
];

export type Migration = (typeof migrationDataList)[number] & { landing_step: number };

interface MigrationOverviewProps {}

export const MigrationOverview: FC<MigrationOverviewProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const refetch = React.useCallback(() => {
    // TODO: Refetch all migration APIs to prevent inconsistencies
    console.log("Refetch!");
  }, []);

  const migrationData = migrationDataList.map((data) => {
    return {
      ...data,
      landing_step:
        data.migration_phase === MigrationPhase["Export Schema"] ||
        data.migration_phase === MigrationPhase["Analyze Schema"] ||
        data.migration_phase === MigrationPhase["Import Schema"]
          ? MigrationStep["Migrate Schema"]
          : data.migration_phase === MigrationPhase["Import Data"] ||
            data.migration_phase === MigrationPhase["Export Data"]
          ? MigrationStep["Migrate Data"]
          : MigrationStep["Verify"],
    };
  });

  const [selectedMigration, setSelectedMigration] = React.useState<Migration>();

  return (
    <Box display="flex" flexDirection="column" gridGap={10}>
      <Box>
        {selectedMigration && (
          <Breadcrumbs aria-label="breadcrumb">
            <Link
              className={classes.link}
              onClick={() => {
                setSelectedMigration(undefined);
              }}
            >
              <Typography variant="body2" color="primary">
                {t("clusterDetail.voyager.migrations")}
              </Typography>
            </Link>
            {selectedMigration && (
              <YBDropdown
                origin={
                  <Box display="flex" alignItems="center" className={classes.dropdownContent}>
                    {selectedMigration.migration_name}
                    <TriangleDownIcon />
                  </Box>
                }
                position={"bottom"}
                growDirection={"right"}
                className={classes.dropdown}
              >
                <Box className={classes.dropdownHeader}>
                  {t("clusterDetail.voyager.migrations")}
                </Box>
                <Box display="flex" flexDirection="column" minWidth="150px">
                  {migrationData.map((migration) => (
                    <MenuItem
                      key={migration.migration_name}
                      selected={migration.migration_name === selectedMigration.migration_name}
                      onClick={() => setSelectedMigration(migration)}
                    >
                      {migration.migration_name}
                    </MenuItem>
                  ))}
                </Box>
              </YBDropdown>
            )}
          </Breadcrumbs>
        )}
      </Box>

      {!selectedMigration ? (
        <MigrationList
          migrationData={migrationData}
          onSelectMigration={setSelectedMigration}
          onRefetch={refetch}
        />
      ) : (
        <MigrationDetails
          steps={migrationSteps}
          migration={selectedMigration}
          onRefetch={refetch}
        />
      )}
    </Box>
  );
};
