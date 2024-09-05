import React, { FC } from "react";
import { Box, LinearProgress, makeStyles, Theme, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import {
  GenericFailure,
  STATUS_TYPES,
  YBAccordion,
  YBButton,
  YBProgress,
  YBStatus,
  YBTable,
} from "@app/components";
import { MigrationPhase } from "../../migration";
import RefreshIcon from "@app/assets/refresh.svg";
import { useGetVoyagerDataMigrationMetricsQuery } from "@app/api/src";

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
}));

const CompletionComponent = (theme: Theme) => (completionPercentage: number) => {
  const statusType =
    completionPercentage === 100
      ? STATUS_TYPES.SUCCESS
      : completionPercentage === 0
      ? STATUS_TYPES.PENDING
      : STATUS_TYPES.IN_PROGRESS;

  return (
    <Box display="flex" alignItems="center" maxWidth={400}>
      <Box mr={0.5}>
        <YBStatus type={statusType} />
      </Box>
      {completionPercentage === 100 && <Box>Complete</Box>}
      {completionPercentage === 0 && <Box>Not started</Box>}
      {completionPercentage !== 100 && completionPercentage !== 0 && (
        <>
          <Box mr={1}>{completionPercentage}%</Box>
          <YBProgress value={completionPercentage} color={theme.palette.primary[500]} />
        </>
      )}
    </Box>
  );
};

interface MigrationProps {
  heading: string;
  migration: Migration;
  step: number;
  onRefetch: () => void;
  isFetching?: boolean;
}

export const MigrationData: FC<MigrationProps> = ({
  heading,
  migration,
  onRefetch,
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

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
          table_name: data.table_name,
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
          table_name: data.table_name,
          importPercentage:
            data.count_live_rows && data.count_total_rows
              ? Math.floor((data.count_live_rows / data.count_total_rows) * 100)
              : 0,
        })),
    [dataAPI]
  );

  const totalExportProgress = Math.floor(
    migrationExportProgressData.reduce((acc, { exportPercentage }) => acc + exportPercentage, 0) /
      (migrationExportProgressData.length || 1)
  );
  const totalImportProgress = Math.floor(
    migrationImportProgressData.reduce((acc, { importPercentage }) => acc + importPercentage, 0) /
      (migrationImportProgressData.length || 1)
  );

  const phase = migration.migration_phase ? migration.migration_phase : 0;

  const migrationImportColumns = [
    {
      name: "table_name",
      label: t("clusterDetail.voyager.tableName"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "importPercentage",
      label: t("clusterDetail.voyager.status"),
      options: {
        customBodyRender: CompletionComponent(theme),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

  const migrationExportColumns = [
    {
      name: "table_name",
      label: t("clusterDetail.voyager.tableName"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
    {
      name: "exportPercentage",
      label: t("clusterDetail.voyager.status"),
      options: {
        customBodyRender: CompletionComponent(theme),
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
      },
    },
  ];

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
          {phase > MigrationPhase["Import Data"] && (
            <Box display="flex" gridGap={4} alignItems="center" mb={5}>
              <YBStatus type={STATUS_TYPES.SUCCESS} size={42} />
              <Box display="flex" flexDirection="column">
                <Typography variant="h5">
                  {t("clusterDetail.voyager.migrateData.migratedData")}
                </Typography>
                <Typography variant="body2">
                  {t("clusterDetail.voyager.migrateData.migratedDataDesc")}
                </Typography>
              </Box>
            </Box>
          )}

          <Box display="flex" gridGap={theme.spacing(2)} flexDirection="column">
            <YBAccordion
              titleContent={
                <AccordionTitleComponent
                  title={t("clusterDetail.voyager.exportData")}
                  status={
                    migrationExportProgressData.length === 0
                      ? STATUS_TYPES.PENDING
                      : totalExportProgress === 100
                      ? STATUS_TYPES.SUCCESS
                      : STATUS_TYPES.IN_PROGRESS
                  }
                />
              }
              defaultExpanded={
                phase === MigrationPhase["Export Data"] ||
                (migrationExportProgressData.length > 0 && totalExportProgress < 100)
              }
            >
              <Box flex={1} minWidth={0}>
                <Box mx={2} mb={5} mt={2}>
                  <Box display="flex" alignItems="center" justifyContent="space-between" mb={1}>
                    <Typography variant="body1">
                      {t("clusterDetail.voyager.percentCompleted", {
                        percent: totalExportProgress,
                      })}
                    </Typography>
                  </Box>
                  <YBProgress value={totalExportProgress} />
                </Box>
                <Box mt={2}>
                  <YBTable
                    data={migrationExportProgressData}
                    columns={migrationExportColumns}
                    options={{
                      pagination: true,
                    }}
                    withBorder={false}
                  />
                </Box>
              </Box>
            </YBAccordion>
            <YBAccordion
              titleContent={
                <AccordionTitleComponent
                  title={t("clusterDetail.voyager.importData")}
                  status={
                    migrationImportProgressData.length === 0
                      ? STATUS_TYPES.PENDING
                      : totalImportProgress === 100
                      ? STATUS_TYPES.SUCCESS
                      : STATUS_TYPES.IN_PROGRESS
                  }
                />
              }
              defaultExpanded={
                phase === MigrationPhase["Import Data"] ||
                (migrationImportProgressData.length > 0 && totalImportProgress < 100)
              }
            >
              <Box flex={1} minWidth={0}>
                <Box mx={2} mb={5} mt={2}>
                  <Box display="flex" alignItems="center" justifyContent="space-between" mb={1}>
                    <Typography variant="body1">
                      {t("clusterDetail.voyager.percentCompleted", {
                        percent: totalImportProgress,
                      })}
                    </Typography>
                  </Box>
                  <YBProgress value={totalImportProgress} />
                </Box>
                <Box mt={2}>
                  <YBTable
                    data={migrationImportProgressData}
                    columns={migrationImportColumns}
                    options={{
                      pagination: true,
                    }}
                    withBorder={false}
                  />
                </Box>
              </Box>
            </YBAccordion>
          </Box>
        </>
      )}
    </Box>
  );
};

const AccordionTitleComponent: React.FC<{ title: string; status?: STATUS_TYPES }> = ({
  title,
  status,
}) => {
  const theme = useTheme();

  return (
    <Box display="flex" alignItems="center" gridGap={theme.spacing(1.2)} flexGrow={1}>
      <Box mt={0.2}>{title}</Box>
      {status && <YBStatus type={status} />}
    </Box>
  );
};
