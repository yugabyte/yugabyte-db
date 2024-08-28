import React, { FC, useMemo } from "react";
import {
  Box,
  LinearProgress,
  Link,
  makeStyles,
  TablePagination,
  Typography,
  useTheme,
} from "@material-ui/core";
import type { Migration } from "../../MigrationOverview";
import { GenericFailure, YBButton } from "@app/components";
import { useTranslation } from "react-i18next";
import RefreshIcon from "@app/assets/refresh.svg";
import { StepCard } from "../schema/StepCard";
import { Prereqs } from "../schema/Prereqs";
import { StepDetails } from "../schema/StepDetails";
import { useGetVoyagerDataMigrationMetricsQuery } from "@app/api/src";
import { MigrationPhase } from "../../migration";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  tooltip: {
    backgroundColor: theme.palette.common.white,
    padding: theme.spacing(1),
    borderRadius: theme.shape.borderRadius,
    boxShadow: theme.shadows[2],
  },
  progressbar: {
    height: "8px",
    borderRadius: "5px",
  },
  bar: {
    borderRadius: "5px",
  },
  barBg: {
    backgroundColor: theme.palette.grey[200],
  },
}));

const exportDataPrereqs: React.ReactNode[] = [
  <>
    Make sure your cluster size matches the recommendation in{" "}
    <Link
      href="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/assess-migration/"
      target="_blank"
    >
      Assessment
    </Link>
    .
  </>,
  <>
    Modify tables and SQL objects according to the suggested refactoring in{" "}
    <Link
      href="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#analyze-schema"
      target="_blank"
    >
      Schema Analysis
    </Link>
    .
  </>,
];

interface MigrationDataProps {
  heading: string;
  migration: Migration;
  step: number;
  onRefetch: () => void;
  isFetching?: boolean;
}

export const MigrationData: FC<MigrationDataProps> = ({
  heading,
  migration,
  onRefetch,
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const [exportPage, setExportPage] = React.useState<number>(0);
  const [exportPerPage, setExportPerPage] = React.useState<number>(5);

  const [importPage, setImportPage] = React.useState<number>(0);
  const [importPerPage, setImportPerPage] = React.useState<number>(5);

  const [triggerPage, setTriggerPage] = React.useState<number>(0);
  const [triggerPerPage, setTriggerPerPage] = React.useState<number>(5);

  const {
    data,
    isFetching: isFetchingAPI,
    isError: isErrorMigrationMetrics,
  } = useGetVoyagerDataMigrationMetricsQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  });

  const dataAPI = { metrics: data?.metrics || [] };

  const migrationExportProgressData = React.useMemo(
    () =>
      dataAPI.metrics
        .filter((m) => m.migration_phase === MigrationPhase["Export Data"])
        .map((data) => ({
          table_name: data.table_name || "",
          exportPercentage:
            data.count_live_rows && data.count_total_rows
              ? Math.floor((data.count_live_rows / data.count_total_rows) * 100)
              : 0,
        })),
    [dataAPI]
  );

  const migrationImportProgressData = React.useMemo(
    () =>
      dataAPI.metrics
        .filter((m) => m.migration_phase === MigrationPhase["Import Data"])
        .map((data) => ({
          table_name: data.table_name || "",
          importPercentage:
            data.count_live_rows && data.count_total_rows
              ? Math.floor((data.count_live_rows / data.count_total_rows) * 100)
              : 0,
        })),
    [dataAPI]
  );

  const migrationTriggerData = [1, 2, 3, 4];

  const phase = migration.migration_phase || 0;

  const exportProgress = useMemo(() => {
    const totalObjects = migrationExportProgressData.length || 1;
    const completedObjects = migrationExportProgressData.filter(
      ({ exportPercentage }) => exportPercentage === 100
    ).length;
    const totalProgress = Math.floor(
      migrationExportProgressData.reduce((acc, { exportPercentage }) => acc + exportPercentage, 0) /
        totalObjects
    );
    return { totalProgress, completedObjects, totalObjects };
  }, [migrationExportProgressData]);

  const importProgress = useMemo(() => {
    const totalObjects = migrationImportProgressData.length || 1;
    const completedObjects = migrationImportProgressData.filter(
      ({ importPercentage }) => importPercentage === 100
    ).length;
    const totalProgress = Math.floor(
      migrationImportProgressData.reduce((acc, { importPercentage }) => acc + importPercentage, 0) /
        totalObjects
    );
    return { totalProgress, completedObjects, totalObjects };
  }, [migrationImportProgressData]);

  const paginatedExportData = useMemo(() => {
    return migrationExportProgressData.slice(
      exportPage * exportPerPage,
      exportPage * exportPerPage + exportPerPage
    );
  }, [migrationExportProgressData, exportPage, exportPerPage]);

  const paginatedImportData = useMemo(() => {
    return migrationImportProgressData.slice(
      importPage * importPerPage,
      importPage * importPerPage + importPerPage
    );
  }, [migrationImportProgressData, importPage, importPerPage]);

  const paginatedTriggerData = useMemo(() => {
    return migrationTriggerData.slice(
      triggerPage * triggerPerPage,
      triggerPage * triggerPerPage + triggerPerPage
    );
  }, [migrationTriggerData, triggerPage, triggerPerPage]);

  return (
    <Box>
      <Box display="flex" justifyContent="space-between" alignItems="start">
        <Typography variant="h4" className={classes.heading}>
          {heading}
        </Typography>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefetch}>
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
      </Box>

      {isErrorMigrationMetrics && <GenericFailure />}

      {(isFetching || isFetchingAPI) && (
        <Box textAlign="center" pt={2} pb={2} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!(isFetching || isFetchingAPI || isErrorMigrationMetrics) && (
        <>
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
            <StepCard
              title={t("clusterDetail.voyager.migrateData.dataExportSourceDB")}
              renderChips={() =>
                `${exportProgress.completedObjects}/${exportProgress.totalObjects} objects completed`
              }
              isLoading={
                phase === MigrationPhase["Export Data"] ||
                (migrationExportProgressData.length > 0 && exportProgress.totalProgress < 100)
              }
              accordion={
                phase === MigrationPhase["Export Data"] ||
                (migrationExportProgressData.length > 0 && exportProgress.totalProgress < 100)
              }
              isDone={exportProgress.totalProgress === 100}
            >
              {(status) => {
                if (status === "TODO") {
                  return (
                    <>
                      <Prereqs items={exportDataPrereqs} />
                      <StepDetails
                        heading={t("clusterDetail.voyager.migrateData.dataExport")}
                        message={t("clusterDetail.voyager.migrateData.dataExportDesc")}
                        docsText={t("clusterDetail.voyager.migrateData.dataExportLearn")}
                        docsLink="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#export-data"
                      />
                    </>
                  );
                }

                if (status === "IN_PROGRESS") {
                  return (
                    <Box
                      display="flex"
                      flexDirection="column"
                      gridGap={theme.spacing(4.5)}
                      mt={3.5}
                    >
                      {paginatedExportData.map(({ table_name, exportPercentage }) => (
                        <Box>
                          <Box display="flex" justifyContent="space-between" mb={1.5}>
                            <Typography variant="body2">{table_name}</Typography>
                            <Typography variant="body2">{exportPercentage}% completed</Typography>
                          </Box>
                          <LinearProgress
                            classes={{
                              root: classes.progressbar,
                              colorPrimary: classes.barBg,
                              bar: classes.bar,
                            }}
                            variant="determinate"
                            value={exportPercentage}
                          />
                        </Box>
                      ))}
                      <Box ml="auto">
                        <TablePagination
                          component="div"
                          count={migrationExportProgressData.length}
                          page={exportPage}
                          onPageChange={(_, newPage) => setExportPage(newPage)}
                          rowsPerPageOptions={[5, 10, 20]}
                          rowsPerPage={exportPerPage}
                          onRowsPerPageChange={(e) =>
                            setExportPerPage(parseInt(e.target.value, 10))
                          }
                        />
                      </Box>
                    </Box>
                  );
                }

                return null;
              }}
            </StepCard>

            <StepCard
              title={t("clusterDetail.voyager.migrateData.dataImportTargetDB")}
              renderChips={() =>
                `${importProgress.completedObjects}/${importProgress.totalObjects} objects completed`
              }
              isLoading={
                phase === MigrationPhase["Import Data"] ||
                (migrationImportProgressData.length > 0 && importProgress.totalProgress < 100)
              }
              accordion={
                phase === MigrationPhase["Import Data"] ||
                (migrationImportProgressData.length > 0 && importProgress.totalProgress < 100)
              }
              isDone={importProgress.totalProgress === 100}
            >
              {(status) => {
                if (status === "TODO") {
                  return (
                    <>
                      <StepDetails
                        heading={t("clusterDetail.voyager.migrateData.dataImport")}
                        message={t("clusterDetail.voyager.migrateData.dataImportDesc")}
                        docsText={t("clusterDetail.voyager.migrateData.dataImportLearn")}
                        docsLink="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#import-data"
                      />
                    </>
                  );
                }

                if (status === "IN_PROGRESS") {
                  return (
                    <Box
                      display="flex"
                      flexDirection="column"
                      gridGap={theme.spacing(4.5)}
                      mt={3.5}
                    >
                      {paginatedImportData.map(({ table_name, importPercentage }) => (
                        <Box>
                          <Box display="flex" justifyContent="space-between" mb={1.5}>
                            <Typography variant="body2">{table_name}</Typography>
                            <Typography variant="body2">{importPercentage}% completed</Typography>
                          </Box>
                          <LinearProgress
                            classes={{
                              root: classes.progressbar,
                              colorPrimary: classes.barBg,
                              bar: classes.bar,
                            }}
                            variant="determinate"
                            value={importPercentage}
                          />
                        </Box>
                      ))}
                      <Box ml="auto">
                        <TablePagination
                          component="div"
                          count={migrationImportProgressData.length}
                          page={importPage}
                          onPageChange={(_, newPage) => setImportPage(newPage)}
                          rowsPerPageOptions={[5, 10, 20]}
                          rowsPerPage={importPerPage}
                          onRowsPerPageChange={(e) =>
                            setImportPerPage(parseInt(e.target.value, 10))
                          }
                        />
                      </Box>
                    </Box>
                  );
                }

                return null;
              }}
            </StepCard>
            <StepCard
              title={t("clusterDetail.voyager.migrateData.indexTriggerTargetDB")}
              renderChips={() => `${12}/${25} objects completed`}
            >
              {(status) => {
                if (status === "TODO") {
                  return (
                    <>
                      <StepDetails
                        heading={t("clusterDetail.voyager.migrateData.indexTrigger")}
                        message={t("clusterDetail.voyager.migrateData.indexTriggerDesc")}
                        docsText={t("clusterDetail.voyager.migrateData.indexTriggerLearn")}
                        docsLink="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#import-data-status"
                      />
                    </>
                  );
                }

                if (status === "IN_PROGRESS") {
                  return (
                    <Box
                      display="flex"
                      flexDirection="column"
                      gridGap={theme.spacing(4.5)}
                      mt={3.5}
                    >
                      {paginatedTriggerData.map(() => (
                        <Box>
                          <Box display="flex" justifyContent="space-between" mb={1.5}>
                            <Typography variant="body2">Object name</Typography>
                            <Typography variant="body2">{44}% completed</Typography>
                          </Box>
                          <LinearProgress
                            classes={{
                              root: classes.progressbar,
                              colorPrimary: classes.barBg,
                              bar: classes.bar,
                            }}
                            variant="determinate"
                            value={44}
                          />
                        </Box>
                      ))}
                      <Box ml="auto">
                        <TablePagination
                          component="div"
                          count={migrationTriggerData.length}
                          page={triggerPage}
                          onPageChange={(_, newPage) => setTriggerPage(newPage)}
                          rowsPerPageOptions={[5, 10, 20]}
                          rowsPerPage={triggerPerPage}
                          onRowsPerPageChange={(e) =>
                            setTriggerPerPage(parseInt(e.target.value, 10))
                          }
                        />
                      </Box>
                    </Box>
                  );
                }

                return null;
              }}
            </StepCard>
          </Box>
        </>
      )}
    </Box>
  );
};
