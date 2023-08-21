import React, { FC } from "react";
import { Box, Breadcrumbs, Link, makeStyles, MenuItem, Typography } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBDropdown } from "@app/components";
import { MigrationList } from "./MigrationList";
import TriangleDownIcon from "@app/assets/caret-down.svg";
import { MigrationData as MigrationSteps } from "./MigrationSteps";
import { MigrationPhase } from "./MigrationPhase";

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
    database_name: "DMS",
    schema_name: "yugabyted",
    status: "In progress",
    complexity: "",
    invocation_timestamp: "11/07/2022, 09:55",
  },
  {
    migration_uuid: "231cdd15-3a91-11ee-89b8-42010a9601e4",
    migration_name: "Migration Name2",
    migration_phase: 1,
    database_name: "DMS",
    schema_name: "yugabyted",
    status: "In progress",
    complexity: "Easy",
    invocation_timestamp: "11/07/2022, 09:55",
  },
  {
    migration_uuid: "231cdd15-3a91-11ee-89b8-42010a9601e4",
    migration_name: "Migration Name3",
    migration_phase: 2,
    database_name: "DMS",
    schema_name: "yugabyted",
    status: "In progress",
    complexity: "Medium",
    invocation_timestamp: "11/07/2022, 09:55",
  },
  {
    migration_uuid: "de3cdd86-3a91-11ee-89b8-42010a9601de",
    migration_name: "Migration Name4",
    migration_phase: 3,
    database_name: "DMS",
    schema_name: "yugabyted",
    status: "Completed",
    complexity: "Hard",
    invocation_timestamp: "11/07/2022, 09:55",
  },
];

export type Migration = (typeof migrationDataList)[number] & { current_phase: string };

interface MigrationOverviewProps {}

export const MigrationOverview: FC<MigrationOverviewProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const migrationSteps = [
    t("clusterDetail.voyager.planAndAssess"),
    t("clusterDetail.voyager.migrateSchema"),
    t("clusterDetail.voyager.migrateData"),
    t("clusterDetail.voyager.verify"),
  ];

  const migrationData = migrationDataList.map((data) => {
    return {
      ...data,
      current_phase: migrationSteps[data.migration_phase],
    };
  });

  const [selectedMigration, setSelectedMigration] = React.useState<Migration>();
  const [selectedPhase, setSelectedPhase] = React.useState<number>();

  return (
    <Box display="flex" flexDirection="column" gridGap={10}>
      <Box>
        {selectedMigration && (
          <Breadcrumbs aria-label="breadcrumb">
            <Link
              className={classes.link}
              onClick={() => {
                setSelectedMigration(undefined);
                setSelectedPhase(undefined);
              }}
            >
              <Typography variant="body2" color="primary">
                {t("clusterDetail.voyager.migrations")}
              </Typography>
            </Link>
            {selectedMigration && selectedPhase == null && (
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
            {selectedMigration && selectedPhase != null && (
              <Link
                className={classes.link}
                onClick={() => {
                  setSelectedPhase(undefined);
                }}
              >
                <Typography variant="body2" color="primary">
                  {selectedMigration.migration_name}
                </Typography>
              </Link>
            )}
            {selectedPhase != null && (
              <YBDropdown
                origin={
                  <Box display="flex" alignItems="center" className={classes.dropdownContent}>
                    {migrationSteps[selectedPhase]}
                    <TriangleDownIcon />
                  </Box>
                }
                position={"bottom"}
                growDirection={"right"}
                className={classes.dropdown}
              >
                <Box className={classes.dropdownHeader}>
                  {t("clusterDetail.voyager.migrationPhases")}
                </Box>
                <Box display="flex" flexDirection="column" minWidth="150px">
                  {migrationSteps.map((step, index) => (
                    <MenuItem
                      key={step}
                      selected={selectedPhase === index}
                      onClick={() => setSelectedPhase(index)}
                      disabled={index > selectedMigration.migration_phase}
                    >
                      {step}
                    </MenuItem>
                  ))}
                </Box>
              </YBDropdown>
            )}
          </Breadcrumbs>
        )}
      </Box>

      {!selectedMigration && (
        <MigrationList migrationData={migrationData} onSelectMigration={setSelectedMigration} />
      )}

      {selectedMigration && selectedPhase == null && (
        <MigrationSteps
          steps={migrationSteps}
          migration={selectedMigration}
          onSelectPhase={setSelectedPhase}
        />
      )}

      {selectedMigration && selectedPhase != null && (
        <MigrationPhase
          steps={migrationSteps}
          migration={selectedMigration}
          phase={selectedPhase}
        />
      )}
    </Box>
  );
};
