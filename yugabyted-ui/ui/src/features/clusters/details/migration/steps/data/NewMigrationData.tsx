import React, { FC } from "react";
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
  /* migration, */
  onRefetch,
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const [page, setPage] = React.useState<number>(0);
  const [perPage, setPerPage] = React.useState<number>(5);

  const isErrorMigrationMetrics = false;
  const isFetchingAPI = false;

  const totalObjects = 25;
  const completedObjects = 12;
  const progress = Math.round((completedObjects / totalObjects) * 100);

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
              renderChips={() => `${completedObjects}/${totalObjects} objects completed`}
              isDone
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
                      {[1, 2, 3, 4].map(() => (
                        <Box>
                          <Box display="flex" justifyContent="space-between" mb={1.5}>
                            <Typography variant="body2">Object name</Typography>
                            <Typography variant="body2">{progress}% completed</Typography>
                          </Box>
                          <LinearProgress
                            classes={{
                              root: classes.progressbar,
                              colorPrimary: classes.barBg,
                              bar: classes.bar,
                            }}
                            variant="determinate"
                            value={progress}
                          />
                        </Box>
                      ))}
                      <Box ml="auto">
                        <TablePagination
                          component="div"
                          count={/* filteredData?.length || */ 25}
                          page={page}
                          onPageChange={(_, newPage) => setPage(newPage)}
                          rowsPerPageOptions={[5, 10, 20]}
                          rowsPerPage={perPage}
                          onRowsPerPageChange={(e) => setPerPage(parseInt(e.target.value, 10))}
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
              renderChips={() => `${completedObjects}/${totalObjects} objects completed`}
              isLoading
              accordion
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
                      {[1, 2, 3, 4].map(() => (
                        <Box>
                          <Box display="flex" justifyContent="space-between" mb={1.5}>
                            <Typography variant="body2">Object name</Typography>
                            <Typography variant="body2">{progress}% completed</Typography>
                          </Box>
                          <LinearProgress
                            classes={{
                              root: classes.progressbar,
                              colorPrimary: classes.barBg,
                              bar: classes.bar,
                            }}
                            variant="determinate"
                            value={progress}
                          />
                        </Box>
                      ))}
                      <Box ml="auto">
                        <TablePagination
                          component="div"
                          count={/* filteredData?.length || */ 25}
                          page={page}
                          onPageChange={(_, newPage) => setPage(newPage)}
                          rowsPerPageOptions={[5, 10, 20]}
                          rowsPerPage={perPage}
                          onRowsPerPageChange={(e) => setPerPage(parseInt(e.target.value, 10))}
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
              renderChips={() => `${completedObjects}/${totalObjects} objects completed`}
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
                      {[1, 2, 3, 4].map(() => (
                        <Box>
                          <Box display="flex" justifyContent="space-between" mb={1.5}>
                            <Typography variant="body2">Object name</Typography>
                            <Typography variant="body2">{progress}% completed</Typography>
                          </Box>
                          <LinearProgress
                            classes={{
                              root: classes.progressbar,
                              colorPrimary: classes.barBg,
                              bar: classes.bar,
                            }}
                            variant="determinate"
                            value={progress}
                          />
                        </Box>
                      ))}
                      <Box ml="auto">
                        <TablePagination
                          component="div"
                          count={/* filteredData?.length || */ 25}
                          page={page}
                          onPageChange={(_, newPage) => setPage(newPage)}
                          rowsPerPageOptions={[5, 10, 20]}
                          rowsPerPage={perPage}
                          onRowsPerPageChange={(e) => setPerPage(parseInt(e.target.value, 10))}
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
