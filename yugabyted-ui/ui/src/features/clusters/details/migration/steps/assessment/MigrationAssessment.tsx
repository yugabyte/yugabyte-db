import React, { FC } from "react";
import { Box, LinearProgress, makeStyles, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import { MigrationAssessmentReport, useGetMigrationAssessmentInfoQuery } from "@app/api/src";
import { MigrationAssessmentSummary } from "./AssessmentSummary";
import { MigrationSourceEnv } from "./AssessmentSourceEnv";
import { MigrationAssessmentRecommendation } from "./AssessmentRecommendation";
import { MigrationAssessmentRefactoring } from "./AssessmentRefactoring";
import { GenericFailure, YBButton } from "@app/components";
import RefreshIcon from "@app/assets/refresh.svg";

const useStyles = makeStyles((theme) => ({
  tabSectionContainer: {
    display: "flex",
    alignItems: "center",
    width: "100%",
    boxShadow: `inset 0px -1px 0px 0px ${theme.palette.grey[200]}`,
  },
  nextSteps: {
    paddingLeft: theme.spacing(4),
    marginBottom: theme.spacing(4),
  },
  hardComp: {
    color: theme.palette.error.main,
  },
  mediumComp: {
    color: theme.palette.warning[700],
  },
  easyComp: {
    color: theme.palette.success.main,
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textAlign: "left",
  },
}));

interface MigrationAssessmentProps {
  heading: string;
  migration: Migration;
  step: number;
  onRefetch: () => void;
  onStepChange?: (step: number) => void;
  isFetching?: boolean;
}

export const MigrationAssessment: FC<MigrationAssessmentProps> = ({
  /* heading, */
  migration,
  onRefetch,
  /*onStepChange, */
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const {
    data: newMigrationAPIData,
    isFetching: isFetchingAPI,
    isError: isErrorMigrationAssessmentInfo,
  } = useGetMigrationAssessmentInfoQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  });

  const newMigrationAPI = (newMigrationAPIData as MigrationAssessmentReport | undefined)/* ?.data */;

  return (
    <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
      <Box display="flex" justifyContent="space-between" alignItems="center" mb={1}>
        <Typography variant="h4">
          {t("clusterDetail.voyager.planAndAssess.heading")}
        </Typography>
        <YBButton variant="ghost" startIcon={<RefreshIcon />} onClick={onRefetch}>
          {t("clusterDetail.performance.actions.refresh")}
        </YBButton>
        {/* {newMigrationAPI?.completedTime && (
          <Typography variant="body1" className={classes.label}>
            {newMigrationAPI.completedTime}
          </Typography>
        )} */}
      </Box>

      {isErrorMigrationAssessmentInfo && <GenericFailure />}

      {(isFetching || isFetchingAPI) && (
        <Box textAlign="center" pt={2} pb={2} width="100%">
          <LinearProgress />
        </Box>
      )}

      {!(isFetching || isFetchingAPI || isErrorMigrationAssessmentInfo) && (
        <>
          <MigrationAssessmentSummary
            complexity={
              newMigrationAPI?.summary?.migration_complexity || migration.complexity || "N/A"
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
            sqlObjects={newMigrationAPI?.recommended_refactoring}
            unsupportedDataTypes={newMigrationAPI?.unsupported_data_types}
            unsupportedFeatures={newMigrationAPI?.unsupported_features}
            unsupportedFunctions={newMigrationAPI?.unsupported_functions}
          />
        </>
      )}
    </Box>
  );
};
