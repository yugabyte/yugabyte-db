import React, { FC } from "react";
import {
  Box,
  Breadcrumbs,
  LinearProgress,
  Link,
  makeStyles,
  MenuItem,
  Typography,
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { GenericFailure, YBDropdown } from "@app/components";
import { MigrationList } from "./MigrationList";
import TriangleDownIcon from "@app/assets/caret-down.svg";
import { MigrationDetails } from "./MigrationDetails";
import { MigrationPhase, MigrationStep, migrationSteps } from "./migration";
import { useGetVoyagerMigrationTasksQuery, VoyagerMigrationDetails } from "@app/api/src";

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

export type Migration = VoyagerMigrationDetails & { landing_step: MigrationStep };

interface MigrationOverviewProps {}

export const MigrationOverview: FC<MigrationOverviewProps> = () => {
  const classes = useStyles();
  const { t } = useTranslation();

  const {
    data: migrationDataList,
    isFetching: isFetchingMigrationTasks,
    refetch: refetchMigrationTasks,
    isError: isErrorMigrationTasks,
  } = useGetVoyagerMigrationTasksQuery({});

  const refetch = React.useCallback(() => {
    refetchMigrationTasks();
  }, []);

  const migrationData = migrationDataList?.migrations?.map((data) => {
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
                  {migrationData?.map((migration) => (
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

      {isErrorMigrationTasks && <GenericFailure />}

      {isFetchingMigrationTasks && (
        <Box textAlign="center" pt={4} pb={4} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!isFetchingMigrationTasks && migrationData && (
        <>
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
              isFetching={isFetchingMigrationTasks}
            />
          )}
        </>
      )}
    </Box>
  );
};
