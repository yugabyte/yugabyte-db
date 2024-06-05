import React, { FC } from "react";
import { Box, LinearProgress, makeStyles, Typography, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import {
  useGetMigrationAssessmentInfoQuery,
} from "@app/api/src";
import { MigrationAssessmentSummary } from "./AssessmentSummary";
import { MigrationSourceEnv } from "./AssessmentSourceEnv";
import { MigrationAssessmentRecommendation } from "./AssessmentRecommendation";
import { MigrationAssessmentRefactoring } from "./AssessmentRefactoring";
import { newMigration } from "./AssessmentData";
import { GenericFailure } from "@app/components";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(2),
  },
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
  heading,
  migration,
  onRefetch,
  onStepChange,
  isFetching = false,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  /* const {
    data,
    isFetching: isFetchingAPI,
    isError: isErrorMigrationAssessmentDetails,
  } = useGetVoyagerMigrationAssesmentDetailsQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  }); */

  const {
    data: newMigrationAPIData,
    isFetching: isFetchingAPI,
    isError: isErrorMigrationAssessmentInfo,
  } = useGetMigrationAssessmentInfoQuery({
    uuid: migration.migration_uuid || "migration_uuid_not_found",
  });
  const newMigrationAPI = newMigrationAPIData?.data;

  return (
    <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
      <Box display="flex" justifyContent="space-between" alignItems="center">
        <Typography variant="h4" className={classes.heading}>
          {t("clusterDetail.voyager.planAndAssess.heading")}
        </Typography>
        {newMigrationAPI && "completedTime" in newMigrationAPI && newMigrationAPI.completedTime && (
          <Typography variant="body1" className={classes.label}>
            {newMigration.completedTime}
          </Typography>
        )}
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
              newMigrationAPI?.summary?.migration_complexity ||
              migration.complexity ||
              newMigration.summary.complexity
            }
            estimatedMigrationTime={
              newMigrationAPI?.summary?.estimated_migration_time ||
              newMigration.Sizing.SizingRecommendation.EstimatedTimeInMinForImport
            }
            summary={
              newMigrationAPI?.summary?.summary ||
              t("clusterDetail.voyager.planAndAssess.summary.unavailable")
            }
          />

          <MigrationSourceEnv
            vcpu={newMigrationAPI?.source_environment?.total_vcpu ?? newMigration.sourceEnv.vcpu}
            memory={
              newMigrationAPI?.source_environment?.total_memory ?? newMigration.sourceEnv.memory
            }
            disk={
              newMigrationAPI?.source_environment?.total_disk_size ?? newMigration.sourceEnv.disk
            }
            connectionCount={
              newMigrationAPI?.source_environment?.no_of_connections ??
              newMigration.sourceEnv.connectionCount
            }
            tableSize={
              newMigrationAPI?.source_database?.table_size ?? newMigration.sourceEnv.tableSize
            }
            rowCount={
              newMigrationAPI?.source_database?.table_row_count?.toString() ??
              newMigration.sourceEnv.rowCount
            }
            totalSize={
              newMigrationAPI?.source_database?.total_table_size ?? newMigration.sourceEnv.totalSize
            }
            indexSize={
              newMigrationAPI?.source_database?.total_index_size ?? newMigration.sourceEnv.indexSize
            }
          />

          <MigrationAssessmentRecommendation
            migration={migration}
            recommendation={
              newMigrationAPI?.target_recommendations?.recommendation_summary ??
              newMigration.recommendation.description
            }
            nodeCount={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation?.num_nodes ??
              newMigration.recommendation.clusterSize.nodeCount
            }
            vCpuPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.vcpu_per_node ?? newMigration.recommendation.clusterSize.vCpuPerNode
            }
            memoryPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.memory_per_node ?? newMigration.recommendation.clusterSize.memoryPerNode
            }
            optimalSelectConnPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.connections_per_node ??
              newMigration.recommendation.clusterSize.optSelectConnPerNode
            }
            optimalInsertConnPerNode={
              newMigrationAPI?.target_recommendations?.target_cluster_recommendation
                ?.inserts_per_node ?? newMigration.recommendation.clusterSize.optInsertConnPerNode
            }
            colocatedTableCount={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.no_of_colocated_tables ??
              newMigration.recommendation.schemaRecommendation.colocatedTables
            }
            shardedTableCount={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.no_of_sharded_tables ??
              newMigration.recommendation.schemaRecommendation.shardedTables
            }
            colocatedTotalSize={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.total_size_colocated_tables ??
              newMigration.recommendation.schemaRecommendation.colocatedSize
            }
            shardedTotalSize={
              newMigrationAPI?.target_recommendations?.target_schema_recommendation
                ?.total_size_sharded_tables ??
              newMigration.recommendation.schemaRecommendation.shardedSize
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
