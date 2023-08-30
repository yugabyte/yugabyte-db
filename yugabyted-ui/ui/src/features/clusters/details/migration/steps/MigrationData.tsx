import React, { FC } from "react";
import { Box, makeStyles, Theme, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../MigrationOverview";
import { STATUS_TYPES, YBAccordion, YBProgress, YBStatus, YBTable } from "@app/components";
import { MigrationPhase } from "../migration";

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
}

export const MigrationData: FC<MigrationProps> = ({ heading, migration }) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const isRunning =
    migration.migration_phase >= MigrationPhase["Export Data"] &&
    migration.migration_phase <= MigrationPhase["Import Data"];
  // const isComplete = migration.migration_phase > MigrationPhase["Import Data"]; We don't need this as pending state is not available
  // const isPending = !isRunning && !isComplete; Pending state will not be shown for this step

  const migrationProgressData = React.useMemo(
    () =>
      [
        {
          migration_uuid: "34fdb71d-4514-11ee-9019-42010a97001d",
          table_name: "YUGABYTED",
          schema_name: "YUGABYTED.TEST1",
          migration_phase: 2,
          status: 3,
          count_live_rows: 42700,
          count_total_rows: 42700,
          invocation_timestamp: "2023-08-27 22:37:13",
        },
        {
          migration_uuid: "34fdb71d-4514-11ee-9019-42010a97001d",
          table_name: "YUGABYTED",
          schema_name: "YUGABYTED.TEST2",
          migration_phase: 2,
          status: 3,
          count_live_rows: 52700,
          count_total_rows: 52700,
          invocation_timestamp: "2023-08-27 22:37:24",
        },
        {
          migration_uuid: "34fdb71d-4514-11ee-9019-42010a97001d",
          table_name: "YUGABYTED",
          schema_name: "YUGABYTED.TEST3",
          migration_phase: 2,
          status: 3,
          count_live_rows: 62000,
          count_total_rows: 62700,
          invocation_timestamp: "2023-08-27 22:37:50",
        },
        {
          migration_uuid: "34fdb71d-4514-11ee-9019-42010a97001d",
          table_name: "YUGABYTED",
          schema_name: "YUGABYTED.TEST4",
          migration_phase: 2,
          status: 3,
          count_live_rows: 52000,
          count_total_rows: 82700,
          invocation_timestamp: "2023-08-27 22:37:46",
        },
        {
          migration_uuid: "34fdb71d-4514-11ee-9019-42010a97001d",
          table_name: "YUGABYTED",
          schema_name: "YUGABYTED.TEST5",
          migration_phase: 2,
          status: 3,
          count_live_rows: 2700,
          count_total_rows: 82700,
          invocation_timestamp: "2023-08-27 22:38:14",
        },
        {
          migration_uuid: "34fdb71d-4514-11ee-9019-42010a97001d",
          table_name: "YUGABYTED",
          schema_name: "YUGABYTED.TEST6",
          migration_phase: 2,
          status: 3,
          count_live_rows: 0,
          count_total_rows: 157000,
          invocation_timestamp: "2023-08-27 22:39:01",
        },
      ].map((data) => ({
        ...data,
        exportPercentage: migration.migration_phase === MigrationPhase["Export Data"] ? 50 : 100,
        importPercentage:
          migration.migration_phase === MigrationPhase["Import Data"]
            ? Math.floor((data.count_live_rows / data.count_total_rows) * 100)
            : 0,
      })),
    [migration.migration_phase]
  );

  const totalExportProgress = Math.floor(
    migrationProgressData.reduce((acc, { exportPercentage }) => acc + exportPercentage, 0) /
      migrationProgressData.length
  );
  const totalImportProgress = Math.floor(
    migrationProgressData.reduce((acc, { importPercentage }) => acc + importPercentage, 0) /
      migrationProgressData.length
  );

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
      <Typography variant="h4" className={classes.heading}>
        {heading}
      </Typography>

      <Box display="flex" gridGap={4} alignItems="center">
        <YBStatus type={isRunning ? STATUS_TYPES.IN_PROGRESS : STATUS_TYPES.SUCCESS} size={42} />
        <Box display="flex" flexDirection="column">
          <Typography variant="h5">
            {migration.migration_phase === MigrationPhase["Export Data"]
              ? t("clusterDetail.voyager.migrateDataExportData")
              : migration.migration_phase === MigrationPhase["Import Schema"]
              ? t("clusterDetail.voyager.migrateDataImportSchema")
              : migration.migration_phase === MigrationPhase["Import Data"]
              ? t("clusterDetail.voyager.migrateDataImportData")
              : t("clusterDetail.voyager.migrateDataComplete")}
          </Typography>
          <Typography variant="body2">
            {migration.migration_phase === MigrationPhase["Export Data"]
              ? t("clusterDetail.voyager.migrateDataExportDataDesc")
              : migration.migration_phase === MigrationPhase["Import Schema"]
              ? t("clusterDetail.voyager.migrateDataImportSchemaDesc")
              : migration.migration_phase === MigrationPhase["Import Data"]
              ? t("clusterDetail.voyager.migrateDataImportDataDesc")
              : t("clusterDetail.voyager.migrateDataCompleteDesc")}
          </Typography>
        </Box>
      </Box>

      <Box mt={6} display="flex" gridGap={theme.spacing(2)} flexDirection="column">
        <YBAccordion
          titleContent={
            <AccordionTitleComponent
              title={t("clusterDetail.voyager.exportData")}
              status={totalExportProgress === 100 ? STATUS_TYPES.SUCCESS : STATUS_TYPES.IN_PROGRESS}
            />
          }
          defaultExpanded={migration.migration_phase < MigrationPhase["Import Data"]}
        >
          <Box flex={1}>
            <Box mx={2} mb={5} mt={2}>
              <Box display="flex" alignItems="center" justifyContent="space-between" mb={1}>
                <Typography variant="body1">
                  {t("clusterDetail.voyager.percentCompleted", { percent: totalExportProgress })}
                </Typography>
              </Box>
              <YBProgress value={totalExportProgress} />
            </Box>
            <Box mt={2}>
              <YBTable
                data={migrationProgressData}
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
                migration.migration_phase < MigrationPhase["Import Data"]
                  ? STATUS_TYPES.PENDING
                  : totalImportProgress === 100
                  ? STATUS_TYPES.SUCCESS
                  : STATUS_TYPES.IN_PROGRESS
              }
            />
          }
          defaultExpanded={migration.migration_phase === MigrationPhase["Import Data"]}
        >
          <Box flex={1}>
            <Box mx={2} mb={5} mt={2}>
              <Box display="flex" alignItems="center" justifyContent="space-between" mb={1}>
                <Typography variant="body1">
                  {t("clusterDetail.voyager.percentCompleted", { percent: totalImportProgress })}
                </Typography>
              </Box>
              <YBProgress value={totalImportProgress} />
            </Box>
            <Box mt={2}>
              <YBTable
                data={migrationProgressData}
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
