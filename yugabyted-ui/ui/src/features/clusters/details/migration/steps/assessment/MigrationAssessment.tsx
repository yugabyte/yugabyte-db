import React, { FC } from "react";
import { Box, LinearProgress, Link, makeStyles, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import { MigrationAssessmentReport, useGetMigrationAssessmentInfoQuery } from "@app/api/src";
import { MigrationAssessmentSummary } from "./AssessmentSummary";
import { MigrationSourceEnv } from "./AssessmentSourceEnv";
import { MigrationAssessmentRecommendation } from "./AssessmentRecommendation";
import { MigrationAssessmentRefactoring } from "./AssessmentRefactoring";
import { GenericFailure, YBButton, YBCodeBlock } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";
import BookIcon from "@app/assets/book.svg";
import { StepCard } from "../schema/StepCard";

interface MigrationAssessmentProps {
  heading: string;
  migration: Migration | undefined;
  step: number;
  onRefetch: () => void;
  isFetching?: boolean;
  isNewMigration?: boolean;
}

const useStyles = makeStyles((theme) => ({
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
}));

export const MigrationAssessment: FC<MigrationAssessmentProps> = ({
  migration,
  onRefetch,
  isFetching = false,
  isNewMigration = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const INSTALL_VOYAGER_DOCS_URL =
      "https://docs.yugabyte.com/preview/yugabyte-voyager/install-yb-voyager/"
  const ASSESS_MIGRATION_DOCS_URL =
      "https://docs.yugabyte.com/preview/yugabyte-voyager/migrate/assess-migration/"

  // TODO: get correct linux install commands, and a way to distinguish different linux distros.
  const INSTALL_VOYAGER_OS_CMD: { [key: string]: string; } = {
    "linux": "brew tap yugabyte/tap\n" +
             "brew install yb-voyager\n" +
             "yb-voyager version",
    "darwin": "brew tap yugabyte/tap\n" +
              "brew install yb-voyager\n" +
              "yb-voyager version",
  }

  const {
    data: newMigrationAPIData,
    isFetching: isFetchingAPI,
    isError: isErrorMigrationAssessmentInfo,
  } = useGetMigrationAssessmentInfoQuery({
    uuid: migration?.migration_uuid || "migration_uuid_not_found",
  });

  const newMigrationAPI = newMigrationAPIData as MigrationAssessmentReport | undefined;

  return (
    <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
      <Box display="flex" justifyContent="space-between" alignItems="center" mb={1}>
        <Typography variant="h4">{t("clusterDetail.voyager.planAndAssess.heading")}</Typography>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefetch}>
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
        {/* {newMigrationAPI?.completedTime && (
          <Typography variant="body1" className={classes.label}>
            {newMigrationAPI.completedTime}
          </Typography>
        )} */}
      </Box>

      {isErrorMigrationAssessmentInfo && !isNewMigration && <GenericFailure />}

      {(isFetching || isFetchingAPI) && !isNewMigration && (
        <Box textAlign="center" pt={2} pb={2} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!(isFetching || isFetchingAPI || isErrorMigrationAssessmentInfo) && !isNewMigration && (
        <>
          <MigrationAssessmentSummary
            complexity={
              newMigrationAPI?.summary?.migration_complexity || migration?.complexity || "N/A"
            }
            estimatedMigrationTime={newMigrationAPI?.summary?.estimated_migration_time || "N/A"}
            summary={
              newMigrationAPI?.summary?.summary ||
              t("clusterDetail.voyager.planAndAssess.summary.unavailable")
            }
          />

          <MigrationSourceEnv
            vcpu={newMigrationAPI?.source_environment?.total_vcpu ?? "N/A"}
            memory={newMigrationAPI?.source_environment?.total_memory ?? "N/A"}
            disk={newMigrationAPI?.source_environment?.total_disk_size ?? "N/A"}
            connectionCount={newMigrationAPI?.source_environment?.no_of_connections ?? "N/A"}
            tableSize={newMigrationAPI?.source_database?.table_size ?? "N/A"}
            rowCount={newMigrationAPI?.source_database?.table_row_count?.toString() ?? "N/A"}
            totalSize={newMigrationAPI?.source_database?.total_table_size ?? "N/A"}
            indexSize={newMigrationAPI?.source_database?.total_index_size ?? "N/A"}
            migration={migration}
          />

          <MigrationAssessmentRecommendation
            migration={migration}
            recommendation={
              newMigrationAPI?.target_recommendations?.recommendation_summary ?? "N/A"
            }
            nodeCount={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation?.num_nodes ??
              "N/A"
            }
            vCpuPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.vcpu_per_node ?? "N/A"
            }
            memoryPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.memory_per_node ?? "N/A"
            }
            optimalSelectConnPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.connections_per_node ?? "N/A"
            }
            optimalInsertConnPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.inserts_per_node ?? "N/A"
            }
            colocatedTableCount={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.no_of_colocated_tables ?? "N/A"
            }
            shardedTableCount={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.no_of_sharded_tables ?? "N/A"
            }
            colocatedTotalSize={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.total_size_colocated_tables ?? "N/A"
            }
            shardedTotalSize={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.total_size_sharded_tables ?? "N/A"
            }
          />

          <MigrationAssessmentRefactoring
            sqlObjects={newMigrationAPI?.recommended_refactoring?.refactor_details}
            unsupportedDataTypes={newMigrationAPI?.unsupported_data_types}
            unsupportedFeatures={newMigrationAPI?.unsupported_features}
            unsupportedFunctions={newMigrationAPI?.unsupported_functions}
          />
        </>
      )}

      {isNewMigration && (<>
        <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
          <StepCard
            title={t("clusterDetail.voyager.planAndAssess.installVoyager")}
          >
            {() => {
              return <>
                <Box mt={2} mb={2} width="fit-content">
                  <Typography variant="body2">
                    {t("clusterDetail.voyager.planAndAssess.installVoyagerDesc")}
                  </Typography>
                </Box>
                <YBCodeBlock
                  text={INSTALL_VOYAGER_OS_CMD["linux"]} // TODO: replace hard coded OS
                  showCopyIconButton={true}
                  preClassName={classes.commandCodeBlock}
                />
                <Box mt={2} mb={2} width="fit-content">
                  <Link
                    className={classes.docsLink}
                    href={INSTALL_VOYAGER_DOCS_URL}
                    target="_blank"
                  >
                    <BookIcon className={classes.menuIcon} />
                    <Typography
                      variant="body2"
                    >
                      {t("clusterDetail.voyager.planAndAssess.installVoyagerLearn")}
                    </Typography>
                  </Link>
                </Box>
              </>
            }}
          </StepCard>
          <StepCard
            title={t("clusterDetail.voyager.planAndAssess.assessMigration")}
          >
            {() => {
              return <>
                <Box mt={2} mb={2} width="fit-content">
                  <Typography variant="body2">
                    {t("clusterDetail.voyager.planAndAssess.assessMigrationDesc")}
                  </Typography>
                </Box>
                <YBCodeBlock
                  text={
                    "# Replace the argument values with those applicable " +
                    "for your migration\n" +
                    "yb-voyager assess-migration\n" +
                    "--source-db-type <SOURCE_DB_TYPE> \\\n" +
                    "--source-db-host <SOURCE_DB_HOST> \\\n" +
                    "--source-db-user <SOURCE_DB_USER> \\\n" +
                    "--source-db-password <SOURCE_DB_PASSWORD> \\\n" +
                    "--source-db-name <SOURCE_DB_NAME> \\\n" +
                    "--source-db-schema <SOURCE_DB_SCHEMA1>,<SOURCE_DB_SCHEMA2> \\\n" +
                    "--export-dir <EXPORT/DIR/PATH> \\\n" +
                    "--start-clean=true"
                  }
                  showCopyIconButton={true}
                  preClassName={classes.commandCodeBlock}
                />
                <Box mt={2} mb={2} width="fit-content">
                  <Link
                    className={classes.docsLink}
                    href={ASSESS_MIGRATION_DOCS_URL}
                    target="_blank"
                  >
                    <BookIcon className={classes.menuIcon} />
                    <Typography
                      variant="body2"
                    >
                      {t("clusterDetail.voyager.planAndAssess.assessMigrationLearn")}
                    </Typography>
                  </Link>
                </Box>
              </>
            }}
          </StepCard>
        </Box>
      </>)}
    </Box>
  );
};
