import React, { FC } from "react";
import { Box, LinearProgress, Link, makeStyles, Typography, useTheme } from "@material-ui/core";
import type { Migration } from "../../MigrationOverview";
import { GenericFailure, YBButton } from "@app/components";
import { useTranslation } from "react-i18next";
import RefreshIcon from "@app/assets/refresh.svg";
import { Prereqs } from "./Prereqs";
import { StepDetails } from "./StepDetails";
import { StepCard } from "./StepCard";
import { SchemaAnalysis } from "./SchemaAnalysis";
import { MigrateSchemaTaskInfo, useGetVoyagerMigrateSchemaTasksQuery } from "@app/api/src";

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

const exportSchemaPrereqs: React.ReactNode[] = [
  <>
    Ensure{" "}
    <Link
      href="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#prepare-the-source-database"
      target="_blank"
    >
      the source and target database are prepared
    </Link>{" "}
    for migration.
  </>,
  <>
    <Link
      href="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#create-an-export-directory"
      target="_blank"
    >
      Create an export directory
    </Link>{" "}
    on the Voyager instance to track the migration state including exported schema and data.
  </>,
];

const importSchemaPrereqs: React.ReactNode[] = [
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
    Modify tables and schema according to the DDL recommendations in{" "}
    <Link
      href="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/assess-migration/"
      target="_blank"
    >
      Assessment
    </Link>
    .
  </>,
];

interface MigrationSchemaProps {
  heading: string;
  migration: Migration;
  step: number;
  onRefetch: () => void;
  isFetching?: boolean;
}

export const MigrationSchema: FC<MigrationSchemaProps> = ({
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
    isError: isErrorMigrationSchemaTasks,
  } = useGetVoyagerMigrateSchemaTasksQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  });

  const schemaAPI = (data as MigrateSchemaTaskInfo) || {};

  const totalObjects = 25;
  const completedObjects = 12;
  const progress = Math.round((completedObjects / totalObjects) * 100);

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

      {isErrorMigrationSchemaTasks && <GenericFailure />}

      {(isFetching || isFetchingAPI) && (
        <Box textAlign="center" pt={2} pb={2} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!(isFetching || isFetchingAPI || isErrorMigrationSchemaTasks) && (
        <>
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
            <StepCard
              title={t("clusterDetail.voyager.migrateSchema.schemaExportSourceDB")}
              isDone={schemaAPI.export_schema === "complete"}
              isLoading={schemaAPI.export_schema === "in-progress"}
              showTodo={!schemaAPI.export_schema || schemaAPI.export_schema === "N/A"}
            >
              {(status) => {
                if (status === "TODO") {
                  return (
                    <>
                      <Prereqs items={exportSchemaPrereqs} />
                      <StepDetails
                        heading={t("clusterDetail.voyager.migrateSchema.schemaExport")}
                        message={t("clusterDetail.voyager.migrateSchema.schemaExportDesc")}
                        docsText={t("clusterDetail.voyager.migrateSchema.schemaExportLearn")}
                        docsLink="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#export-schema"
                      />
                    </>
                  );
                }

                if (status === "IN_PROGRESS") {
                  return (
                    <>
                      <LinearProgress
                        classes={{
                          root: classes.progressbar,
                          colorPrimary: classes.barBg,
                          bar: classes.bar,
                        }}
                        variant="determinate"
                        value={progress}
                      />
                      <Box ml="auto" mt={1} width="fit-content">
                        <Typography variant="body2">
                          {completedObjects}/{totalObjects} objects completed
                        </Typography>
                      </Box>
                    </>
                  );
                }

                return null;
              }}
            </StepCard>
            <StepCard
              title={t("clusterDetail.voyager.migrateSchema.schemaAnalysis")}
              isDone={schemaAPI.analyze_schema === "complete"}
              isLoading={schemaAPI.analyze_schema === "in-progress"}
              accordion={schemaAPI.analyze_schema === "complete"}
              defaultExpanded={
                schemaAPI.analyze_schema === "complete" &&
                (!schemaAPI.import_schema || schemaAPI.import_schema === "N/A")
              }
              showTooltip={
                schemaAPI.export_schema !== "complete" &&
                (!schemaAPI.analyze_schema || schemaAPI.analyze_schema === "N/A")
              }
              showTodo={
                schemaAPI.export_schema === "complete" &&
                (!schemaAPI.analyze_schema || schemaAPI.analyze_schema === "N/A")
              }
              hideContent={
                schemaAPI.export_schema !== "complete" &&
                (!schemaAPI.analyze_schema || schemaAPI.analyze_schema === "N/A")
              }
            >
              {(status) => {
                if (status === "TODO") {
                  return (
                    <StepDetails
                      heading={t("clusterDetail.voyager.migrateSchema.schemaAnalysis")}
                      message={t("clusterDetail.voyager.migrateSchema.schemaAnalysisDesc")}
                      docsText={t("clusterDetail.voyager.migrateSchema.schemaAnalysisLearn")}
                      docsLink="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#analyze-schema"
                    />
                  );
                }

                if (status === "IN_PROGRESS") {
                  return (
                    <>
                      <LinearProgress
                        classes={{
                          root: classes.progressbar,
                          colorPrimary: classes.barBg,
                          bar: classes.bar,
                        }}
                        variant="determinate"
                        value={progress}
                      />
                      <Box ml="auto" mt={1} width="fit-content">
                        <Typography variant="body2">
                          {completedObjects}/{totalObjects} objects completed
                        </Typography>
                      </Box>
                    </>
                  );
                }

                return <SchemaAnalysis migration={migration} schemaAPI={schemaAPI} />;
              }}
            </StepCard>
            <StepCard
              title={t("clusterDetail.voyager.migrateSchema.schemaImportTargetDB")}
              isDone={schemaAPI.import_schema === "complete"}
              isLoading={schemaAPI.import_schema === "in-progress"}
              showTooltip={
                schemaAPI.analyze_schema !== "complete" &&
                (!schemaAPI.import_schema || schemaAPI.import_schema === "N/A")
              }
              showTodo={
                schemaAPI.analyze_schema === "complete" &&
                (!schemaAPI.import_schema || schemaAPI.import_schema === "N/A")
              }
              hideContent={
                schemaAPI.analyze_schema !== "complete" &&
                (!schemaAPI.import_schema || schemaAPI.import_schema === "N/A")
              }
            >
              {(status) => {
                if (status === "TODO") {
                  return (
                    <>
                      <Prereqs items={importSchemaPrereqs} />
                      <StepDetails
                        heading={t("clusterDetail.voyager.migrateSchema.schemaImport")}
                        message={t("clusterDetail.voyager.migrateSchema.schemaImportDesc")}
                        docsText={t("clusterDetail.voyager.migrateSchema.schemaImportLearn")}
                        docsLink="https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#import-schema"
                      />
                    </>
                  );
                }

                if (status === "IN_PROGRESS") {
                  return (
                    <>
                      <LinearProgress
                        classes={{
                          root: classes.progressbar,
                          colorPrimary: classes.barBg,
                          bar: classes.bar,
                        }}
                        variant="determinate"
                        value={progress}
                      />
                      <Box ml="auto" mt={1} width="fit-content">
                        <Typography variant="body2">
                          {completedObjects}/{totalObjects} objects completed
                        </Typography>
                      </Box>
                    </>
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
