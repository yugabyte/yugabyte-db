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
import { YBDropdown } from "@app/components";
import { MigrationList } from "./MigrationList";
import TriangleDownIcon from "@app/assets/caret-down.svg";
import { MigrationDetails } from "./MigrationDetails";
import { MigrationPhase, MigrationStep, migrationSteps } from "./migration";
import { useGetVoyagerMigrationTasksQuery, VoyagerMigrationDetails } from "@app/api/src";
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

  // DATO
  const {
    data: migrationDataListo,
    isLoading: isLoadingMigrationTasks,
    isFetching: isFetchingMigrationTasks,
    refetch: refetchMigrationTasks,
    isError: isErrorMigrationTaskso,
  } = useGetVoyagerMigrationTasksQuery({});

  const migrationDataList: typeof migrationDataListo = React.useMemo(
    () => ({
      migrations: [
        {
          migration_uuid: "c8fc9318-4872-11ee-bdc6-42010a97001c",
          migration_name: "Migration_1693540831345",
          migration_phase: 0,
          invocation_sequence: 2,
          source_db: "oracle",
          complexity: "N/A",
          database_name: "DMS",
          schema_name: "YUGABYTED",
          status: "COMPLETED",
          invocation_timestamp: "2023-09-01 02:54:44",
        },
        {
          migration_uuid: "a728a3d7-486c-11ee-8b83-42010a97001a",
          migration_name: "Migration_164446393009",
          migration_phase: 1,
          invocation_sequence: 4,
          source_db: "",
          complexity: "N/A",
          database_name: "",
          schema_name: "",
          status: "COMPLETED",
          invocation_timestamp: "2023-09-01 02:52:18",
        },
      ],
    }),
    []
  );

  const isErrorMigrationTasks = false;

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
            data.migration_phase === MigrationPhase["Import Schema"]
              ? MigrationStep["Migrate Schema"]
              : data.migration_phase === MigrationPhase["Import Data"] ||
                data.migration_phase === MigrationPhase["Export Data"]
              ? MigrationStep["Migrate Data"]
              : data.migration_phase === MigrationPhase["Analyze Schema"]
              ? MigrationStep["Plan and Assess"]
              : data.migration_phase === MigrationPhase["Verify"]
              ? MigrationStep["Verify"]
              : 0,
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

      {isLoadingMigrationTasks && (
        <Box textAlign="center" pt={4} pb={4} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!isLoadingMigrationTasks && (
        <>
          {!selectedMigration ? (
            <MigrationList
              migrationData={migrationData}
              onSelectMigration={({ migration_uuid }) => setQueryParams({ migration_uuid })}
              hasError={isErrorMigrationTasks}
              onRefresh={refetch}
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
