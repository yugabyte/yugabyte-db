import React, { FC } from "react";
import { Box, LinearProgress, Link, makeStyles, Typography, useTheme } from "@material-ui/core";
import type { Migration } from "../../MigrationOverview";
import { GenericFailure, YBButton, YBCodeBlock } from "@app/components";
import { useTranslation } from "react-i18next";
import RefreshIcon from "@app/assets/refresh.svg";
import BookIcon from "@app/assets/book.svg";
import { Prereqs } from "./Prereqs";
import { StepCard } from "./StepCard";
import { SchemaAnalysis } from "./SchemaAnalysis";
import { MigrateSchemaTaskInfo, useGetVoyagerMigrateSchemaTasksQuery } from "@app/api/src";
import { Trans } from "react-i18next";
import VoyagerVersionBox from "../../VoyagerVersionBox";

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
  commandCodeBlock: {
    lineHeight: 1.5,
    padding: theme.spacing(1.2),
  },
  docsLink: {
    display: "flex",
    paddingRight: theme.spacing(1.5),
  },
  menuIcon: {
    marginRight: theme.spacing(1),
  },
  dividerWrapper: {
    margin: theme.spacing(3, -2, 3, -9)
  },
  divider: {
    backgroundColor: theme.palette.grey[300],
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
  migration: Migration | undefined;
  step: number;
  onRefetch: () => void;
  isFetching?: boolean;
  isNewMigration?: boolean;
  voyagerVersion?: string;
}

export const MigrationSchema: FC<MigrationSchemaProps> = ({
  heading,
  migration,
  onRefetch,
  isFetching = false,
  isNewMigration = false,
  voyagerVersion
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const {
    data,
    isFetching: isFetchingAPI,
    isError: isErrorMigrationSchemaTasks,
  } = useGetVoyagerMigrateSchemaTasksQuery({
    uuid: migration?.migration_uuid || "00000000-0000-0000-0000-000000000000",
  });

  const schemaAPI = (data as MigrateSchemaTaskInfo) || {};
  const EXPORT_SCHEMA_DOCS_URL =
      "https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#export-schema"
  const ANALYZE_SCHEMA_DOCS_URL =
      "https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#analyze-schema"
  const IMPORT_SCHEMA_DOCS_URL =
      "https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/migrate-steps/#import-schema"

  let manualSum: number = (
        schemaAPI?.current_analysis_report?.recommended_refactoring?.reduce(
          (sum, detail) => {
            return sum + (detail?.manual ?? 0);
          }, 0) || 0);

  const manualRefactorRequired: boolean = manualSum !== 0;

  return (
    <Box>
      <Box display="flex" justifyContent="space-between" alignItems="start">
        <Box display="flex" sx={{ position: "relative" }}>
          <Typography variant="h4" className={classes.heading}>
            {heading}
          </Typography>
          {
            !(isFetching && !isNewMigration) && !!voyagerVersion && (
              <Box sx={{ position: "absolute", top: -5, width: "120%", left: 150 }}>
                <VoyagerVersionBox voyagerVersion={voyagerVersion} />
              </Box>
            )
          }
        </Box>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefetch}>
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
      </Box>

      {isErrorMigrationSchemaTasks && !isNewMigration && <GenericFailure />}

      {(isFetching || isFetchingAPI) && !isNewMigration && (
        <Box textAlign="center" pt={2} pb={2} width="100%">
          <LinearProgress />
        </Box>
      )}

      {(!(isFetching || isFetchingAPI || isErrorMigrationSchemaTasks) || isNewMigration) && (
        <>
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
            <StepCard
              title={
                <Trans
                  i18nKey="clusterDetail.voyager.migrateSchema.schemaExportSourceDB"
                  components={[<strong key="0" />]}
                />
              }
              isDone={schemaAPI.export_schema === "complete"}
              isLoading={schemaAPI.export_schema === "in-progress"}
              showInProgress={schemaAPI.export_schema === "in-progress"}
              accordion={!(!schemaAPI.export_schema || schemaAPI.export_schema === "N/A")}
              contentSeparator={true}
            >
              {(status) => {
                if (status === "TODO" || status === "DONE" || status === "IN_PROGRESS") {
                  return (
                    <>
                      <Box my={4}>
                        <Prereqs items={exportSchemaPrereqs}/>
                      </Box>
                      <Box my={2} width="fit-content">
                        <Typography variant="body2">
                          <Trans
                            i18nKey="clusterDetail.voyager.migrateSchema.schemaExportDesc"
                            components={[<strong key="0" />, <strong key="1" />]}
                          />
                        </Typography>
                      </Box>
                      <YBCodeBlock
                        text={
                          "# Replace the argument values with those applicable " +
                          "for your migration\n" +
                          "yb-voyager export schema\n" +
                          "--source-db-type <SOURCE_DB_TYPE> \\\n" +
                          "--source-db-host <SOURCE_DB_HOST> \\\n" +
                          "--source-db-user <SOURCE_DB_USER> \\\n" +
                          "--source-db-password <SOURCE_DB_PASSWORD> \\\n" +
                          "--source-db-name <SOURCE_DB_NAME> \\\n" +
                          "--source-db-schema <SOURCE_DB_SCHEMA> \\\n" +
                          "--export-dir <EXPORT/DIR/PATH> \\\n" +
                          "--start-clean true"
                        }
                        showCopyIconButton={true}
                        preClassName={classes.commandCodeBlock}
                        highlightSyntax={true}
                      />
                      <Box mt={2} mb={2} width="fit-content">
                        <Link
                          className={classes.docsLink}
                          href={EXPORT_SCHEMA_DOCS_URL}
                          target="_blank"
                        >
                          <BookIcon className={classes.menuIcon} />
                          <Typography variant="body2">
                            {t("clusterDetail.voyager.migrateSchema.schemaExportLearn")}
                          </Typography>
                        </Link>
                      </Box>
                    </>
                  );
                }


                return null;
              }}
            </StepCard>
            <StepCard
              title={
                <Trans
                  i18nKey="clusterDetail.voyager.migrateSchema.schemaAnalysis"
                  components={[<strong key="0" />]}
                />
              }
              isDone={schemaAPI.analyze_schema === "complete"}
              isTodo={manualRefactorRequired}
              isLoading={schemaAPI.analyze_schema === "in-progress"}
              showInProgress={schemaAPI.analyze_schema === "in-progress"}
              accordion={schemaAPI.analyze_schema === "complete"}
              contentSeparator={true}
              defaultExpanded={
                (schemaAPI.analyze_schema === "complete" &&
                schemaAPI.export_schema === "complete" &&
                (!schemaAPI.import_schema || schemaAPI.import_schema === "N/A")) ||
                manualRefactorRequired
              }
            >
              {(status) => {
                if (status === "TODO") {
                  return (
                    <>
                      <Box my={2} width="fit-content">
                        <Typography variant="body2">
                          {t("clusterDetail.voyager.migrateSchema.schemaAnalysisDesc")}
                        </Typography>
                      </Box>
                      <YBCodeBlock
                        text={
                          "# Replace the argument values with those applicable " +
                          "for your migration\n" +
                          "yb-voyager analyze-schema " +
                          "--export-dir <EXPORT_DIR> --output-format <FORMAT>"
                        }
                        showCopyIconButton={true}
                        preClassName={classes.commandCodeBlock}
                        highlightSyntax={true}
                      />
                      <Box mt={2} mb={2} width="fit-content">
                        <Link
                          className={classes.docsLink}
                          href={ANALYZE_SCHEMA_DOCS_URL}
                          target="_blank"
                        >
                          <BookIcon className={classes.menuIcon} />
                          <Typography
                            variant="body2"
                          >
                            {t("clusterDetail.voyager.migrateSchema.schemaAnalysisLearn")}
                          </Typography>
                        </Link>
                      </Box>
                    </>
                  );
                }

                if (status === "IN_PROGRESS") {
                  return;
                }

                return <SchemaAnalysis migration={migration} schemaAPI={schemaAPI} />;
              }}
            </StepCard>
            <StepCard
              title={
                <Trans
                  i18nKey="clusterDetail.voyager.migrateSchema.schemaImportTargetDB"
                  components={[<strong key="0" />]}
                />
              }
              isDone={schemaAPI.import_schema === "complete"}
              isLoading={schemaAPI.import_schema === "in-progress"}
              showInProgress={schemaAPI.import_schema === "in-progress"}
              showTooltip={
                schemaAPI.analyze_schema !== "complete" &&
                (!schemaAPI.import_schema || schemaAPI.import_schema === "N/A")
              }
              accordion={
                schemaAPI.analyze_schema === "complete" &&
                !!schemaAPI.import_schema && schemaAPI.import_schema === "complete"
              }
              defaultExpanded={
                schemaAPI.analyze_schema === "complete" &&
                schemaAPI.import_schema === "complete"
              }
              contentSeparator={true}
            >
              {(status) => {
                if (status) {
                  return (
                    <>
                      <Box my={4}>
                        <Prereqs items={importSchemaPrereqs} />
                      </Box>
                      <Box mt={2} mb={2} width="fit-content">
                        <Typography variant="body2">
                          {t("clusterDetail.voyager.migrateSchema.schemaImportDesc")}
                        </Typography>
                      </Box>
                      <YBCodeBlock
                        text={
                          "# Replace the argument values with those applicable " +
                          "for your migration\n" +
                          "yb-voyager import schema\n" +
                          "--target-db-host <TARGET_DB_HOST> \\\n" +
                          "--target-db-user <TARGET_DB_USER> \\\n" +
                          "--target-db-password <TARGET_DB_PASSWORD> \\\n" +
                          "--target-db-name <TARGET_DB_NAME> \\\n" +
                          "--target-db-schema <TARGET_DB_SCHEMA> \\\n" +
                          "--export-dir <EXPORT/DIR/PATH>"
                        }
                        showCopyIconButton={true}
                        preClassName={classes.commandCodeBlock}
                        highlightSyntax={true}
                      />
                      <Box mt={2} mb={2} width="fit-content">
                        <Link
                          className={classes.docsLink}
                          href={IMPORT_SCHEMA_DOCS_URL}
                          target="_blank"
                        >
                          <BookIcon className={classes.menuIcon} />
                          <Typography
                            variant="body2"
                          >
                            {t("clusterDetail.voyager.migrateSchema.schemaImportLearn")}
                          </Typography>
                        </Link>
                      </Box>
                    </>
                  );
                }

                if (status === "IN_PROGRESS") {
                  return;
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
