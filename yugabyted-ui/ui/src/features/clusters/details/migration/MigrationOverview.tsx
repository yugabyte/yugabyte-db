import React, { FC } from "react";
import {
  Box,
  Breadcrumbs,
  LinearProgress,
  Link,
  makeStyles,
  MenuItem,
  Paper,
  Typography,
} from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { GenericFailure, YBButton, YBDropdown } from "@app/components";
import { MigrationList } from "./MigrationList";
import TriangleDownIcon from "@app/assets/caret-down.svg";
import { MigrationDetails } from "./MigrationDetails";
import { MigrationPhase, MigrationStep, migrationSteps } from "./migration";
import { useGetVoyagerMigrationTasksQuery, VoyagerMigrationDetails } from "@app/api/src";
import RefreshIcon from "@app/assets/refresh.svg";
import { StringParam, useQueryParams } from "use-query-params";

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
    marginBottom: theme.spacing(4),
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
    isLoading: isLoadingMigrationTasks,
    isFetching: isFetchingMigrationTasks,
    refetch: refetchMigrationTasks,
    isError: isErrorMigrationTasks,
  } = useGetVoyagerMigrationTasksQuery({});

  const refetch = React.useCallback(() => {
    refetchMigrationTasks();
  }, []);

  const migrationData = React.useMemo(
    () =>
      migrationDataList?.migrations?.map((data) => {
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
      }),
    [migrationDataList]
  );

  const [selectedMigration, setSelectedMigration] = React.useState<Migration>();
  const [{ migration_uuid }, setQueryParams] = useQueryParams({
    migration_uuid: StringParam,
  });

  React.useEffect(() => {
    setSelectedMigration(
      migration_uuid
        ? migrationData?.find((migration) => migration.migration_uuid === migration_uuid)
        : undefined
    );
  }, [migration_uuid, migrationData]);

  return (
    <Box display="flex" flexDirection="column" gridGap={10}>
      <Box>
        {selectedMigration && (
          <Breadcrumbs aria-label="breadcrumb">
            <Link
              className={classes.link}
              onClick={() => {
                setQueryParams({ migration_uuid: undefined });
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
                      onClick={() => setQueryParams({ migration_uuid: migration.migration_uuid })}
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

      {isLoadingMigrationTasks && (
        <Box textAlign="center" pt={4} pb={4} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!isLoadingMigrationTasks && migrationData && (
        <>
          {!selectedMigration ? (
            <Box>
              <Paper>
                <Box p={4}>
                  <Box display="flex" justifyContent="space-between" alignItems="start">
                    <Typography variant="h4" className={classes.heading}>
                      {t("clusterDetail.voyager.migrations")}
                    </Typography>
                    <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={refetch}>
                      {t("clusterDetail.performance.actions.refresh")}
                    </YBButton>
                  </Box>
                  <MigrationList
                    migrationData={migrationData}
                    onSelectMigration={({ migration_uuid }) => setQueryParams({ migration_uuid })}
                  />
                </Box>
              </Paper>
            </Box>
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
