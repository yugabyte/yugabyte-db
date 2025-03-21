import React, { FC } from "react";
import { Box, Divider, Paper, Typography, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import type { Migration } from "../../MigrationOverview";
import SparkIcon from "@app/assets/spark.svg";
import type {
  AssessmentCategoryInfo,
  RefactoringCount,
} from "@app/api/src";
import { MigrationAssessmentRefactoring } from "./AssessmentRefactoring";
import { RecommendedClusterSize } from "./AssessmentRecommendedClusterSize";
import { RecommendedDataDistribution } from "./AssessmentRecommendedDataDistribution";

const useStyles = makeStyles((theme) => ({
  heading: {
    display: "flex",
    alignItems: "center",
    gridGap: theme.spacing(3),
    paddingTop: theme.spacing(3),
    paddingBottom: theme.spacing(3),
    paddingRight: theme.spacing(2),
    paddingLeft: theme.spacing(2),
  },
  paper: {
    overflow: "clip",
    height: "100%",
  },
  boxBody: {
    display: "flex",
    flexDirection: "column",
    gridGap: theme.spacing(2),
    backgroundColor: theme.palette.info[400],
    height: "100%",
    paddingRight: theme.spacing(3),
    paddingLeft: theme.spacing(3),
    paddingTop: theme.spacing(2),
    paddingBottom: theme.spacing(2),
  },
  gradientText: {
    background: "linear-gradient(91deg, #ED35EC 3.97%, #ED35C5 33%, #5E60F0 50%)",
    backgroundClip: "text",
    WebkitBackgroundClip: "text",
    WebkitTextFillColor: "transparent",
  },
}));

interface MigrationAssessmentRecommendationProps {
  migration: Migration | undefined;
  nodeCount: string | number;
  vCpuPerNode: string | number;
  memoryPerNode: string | number;
  optimalSelectConnPerNode: string | number;
  optimalInsertConnPerNode: string | number;
  colocatedTableCount: string | number;
  shardedTableCount: string | number;
  colocatedTotalSize: string | number;
  shardedTotalSize: string | number;
  sqlObjects: RefactoringCount[] | undefined;
  assessmentIssues: AssessmentCategoryInfo[] | undefined;
  targetDBVersion: string | undefined;
  migrationComplexityExplanation: string | undefined;
}

export const MigrationAssessmentRecommendation: FC<MigrationAssessmentRecommendationProps> = ({
  migration,
  nodeCount,
  vCpuPerNode,
  memoryPerNode,
  optimalSelectConnPerNode,
  optimalInsertConnPerNode,
  colocatedTableCount,
  shardedTableCount,
  colocatedTotalSize,
  shardedTotalSize,
  sqlObjects,
  assessmentIssues,
  targetDBVersion,
  migrationComplexityExplanation
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  return (
    <Paper className={classes.paper}>
      <Box height="100%">
        <Box className={classes.heading}>
          <Typography variant="h5">
            {t("clusterDetail.voyager.planAndAssess.recommendation.heading")}
          </Typography>
          <Box display="flex" gridGap={theme.spacing(1)} alignItems="center">
            <Typography variant="body1" className={classes.gradientText}>
              {t("clusterDetail.voyager.planAndAssess.recommendation.poweredByCopilot")}
            </Typography>
            <SparkIcon/>
          </Box>
        </Box>

        <Divider orientation="horizontal" />

        <Box className={classes.boxBody}>
          <RecommendedClusterSize
            nodeCount={nodeCount}
            vCpuPerNode={vCpuPerNode}
            memoryPerNode={memoryPerNode}
            optimalSelectConnPerNode={optimalSelectConnPerNode}
            optimalInsertConnPerNode={optimalInsertConnPerNode}
          />

          <RecommendedDataDistribution
            migration={migration}
            colocatedTableCount={colocatedTableCount}
            shardedTableCount={shardedTableCount}
            colocatedTotalSize={colocatedTotalSize}
            shardedTotalSize={shardedTotalSize}
          />

          <MigrationAssessmentRefactoring
            sqlObjects={sqlObjects}
            assessmentCategoryInfo={assessmentIssues}
            targetDBVersion={targetDBVersion}
            migrationComplexityExplanation={migrationComplexityExplanation}
          />

        </Box>
      </Box>
    </Paper>
  );
};
