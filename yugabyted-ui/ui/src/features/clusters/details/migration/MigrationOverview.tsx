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
import { BooleanParam, StringParam, useQueryParams } from "use-query-params";

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
              ? MigrationStep["Schema Migration"]
              : data.migration_phase === MigrationPhase["Import Data"] ||
                data.migration_phase === MigrationPhase["Export Data"]
              ? MigrationStep["Data Migration"]
              : data.migration_phase === MigrationPhase["Verify"]
              ? MigrationStep["Verification"]
              : 0,
        };
      }),
    [migrationDataList]
  );

  const [selectedMigration, setSelectedMigration] = React.useState<Migration>();
  const [isNewMigration, setIsNewMigration] = React.useState<boolean>(false);
  const [oldMigrationUuids, setOldMigrationUuids] = React.useState<Map<string, number>>(new Map());

  const [{ migration_uuid, new_migration }, setQueryParams] = useQueryParams({
    migration_uuid: StringParam,
    new_migration: BooleanParam,
  });

  // Time that the new migration page page was first opened.
  // Is initially set to 0 and resets if user navigates away from new migration page.
  var currentTime = 0;

  React.useEffect(() => {
    setSelectedMigration(
      migration_uuid
        ? migrationData?.find((migration) => migration.migration_uuid === migration_uuid)
        : undefined
    );
    // Set time if navigated to new migration page
    if (!isNewMigration && new_migration !== undefined) {
      currentTime = new Date().getTime();
    }
    // Reset time if navigated away from migration page
    if (isNewMigration && new_migration === undefined) {
      currentTime = 0;
    }
    setIsNewMigration(new_migration === undefined ? false : true);

    // T
    // Look for new migration_uuid and pick the newest one
    var migrationUuids: Map<string, number> = new Map();
    migrationData?.map((migration) => {
      if (migration?.migration_uuid) {
        var startTime = migration?.start_timestamp ?
          new Date(migration.start_timestamp).getTime() :
          0;
        migrationUuids.set(migration.migration_uuid, startTime);
      }
    });
    var mostRecentUuid: string = "";
    var mostRecentTime: number = currentTime;
    migrationUuids.forEach((time, uuid) => {
      if (!oldMigrationUuids.has(uuid)) {
        if (time > mostRecentTime && mostRecentTime > 0) {
          mostRecentUuid = uuid;
          mostRecentTime = time;
        }
      }
    });
    if (isNewMigration && mostRecentUuid !== "") {
      setQueryParams({ migration_uuid: mostRecentUuid, new_migration: undefined });
    }
    setOldMigrationUuids(migrationUuids);
  }, [migration_uuid, new_migration, migrationData]);

  return (
    <Box display="flex" flexDirection="column" gridGap={10}>
      <Box>
        {(selectedMigration || isNewMigration) && (
          <Breadcrumbs aria-label="breadcrumb">
            <Link
              className={classes.link}
              onClick={() => {
                setQueryParams({ migration_uuid: undefined, new_migration: undefined });
              }}
            >
              <Typography variant="body2" color="primary">
                {t("clusterDetail.voyager.migrations")}
              </Typography>
            </Link>
            {(selectedMigration || isNewMigration) && (
              <YBDropdown
                origin={
                  <Box display="flex" alignItems="center" className={classes.dropdownContent}>
                    {isNewMigration ?
                      t("clusterDetail.voyager.newMigration") :
                      selectedMigration?.migration_name}
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
                      selected={migration.migration_name === selectedMigration?.migration_name}
                      onClick={() => setQueryParams({
                        migration_uuid: migration.migration_uuid,
                        new_migration: undefined,
                      })}
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

      {(isLoadingMigrationTasks || isFetchingMigrationTasks) ? (
          <Box textAlign="center" pt={4} pb={4} width="100%">
            <LinearProgress />
          </Box>
        ) : (
          <>
            {(!selectedMigration && !isNewMigration) ? (
              <MigrationList
                migrationData={migrationData}
                onSelectMigration={({ migration_uuid }) => setQueryParams({
                  migration_uuid,
                  new_migration: undefined,
                })}
                hasError={isErrorMigrationTasks}
                onRefresh={refetch}
                onNewMigration={() => setQueryParams({
                  migration_uuid: undefined,
                  new_migration: null,
                })}
              />
            ) : (
              <MigrationDetails
                steps={migrationSteps}
                migration={selectedMigration}
                onRefetch={refetch}
                isFetching={isFetchingMigrationTasks}
                isNewMigration={isNewMigration}
              />
            )}
          </>
        )
      }
    </Box>
  );
};
