import React, { FC } from "react";
import { Box, Divider, Grid, Paper, Typography, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBTooltip } from "@app/components";
import { MigrationRecommendationSidePanel } from "./AssessmentRecommendationSidePanel";
import type { Migration } from "../../MigrationOverview";
import { convertBytesToGB } from "@app/helpers";

const useStyles = makeStyles((theme) => ({
  heading: {
    marginBottom: theme.spacing(4),
  },
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    marginBottom: theme.spacing(0.75),
    textTransform: "uppercase",
    textAlign: "left",
  },
  dividerVertical: {
    marginLeft: theme.spacing(2.5),
    marginRight: theme.spacing(2.5),
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  pointer: {
    cursor: "pointer",
  },
}));

interface MigrationAssessmentRecommendationProps {
  migration: Migration;
  recommendation: string;
  nodeCount: string | number;
  vCpuPerNode: string | number;
  memoryPerNode: string | number;
  optimalSelectConnPerNode: string | number;
  optimalInsertConnPerNode: string | number;
  colocatedTableCount: string | number;
  shardedTableCount: string | number;
  colocatedTotalSize: string | number;
  shardedTotalSize: string | number;
}

export const MigrationAssessmentRecommendation: FC<MigrationAssessmentRecommendationProps> = ({
  migration,
  recommendation,
  nodeCount,
  vCpuPerNode,
  memoryPerNode,
  optimalSelectConnPerNode,
  optimalInsertConnPerNode,
  colocatedTableCount,
  shardedTableCount,
  colocatedTotalSize,
  shardedTotalSize,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  const [showRecommendation, setShowRecommendation] = React.useState<boolean>(false);

  return (
    <Paper>
      <Box px={2} py={3}>
        <Box
          display="flex"
          justifyContent="space-between"
          alignItems="center"
          className={classes.heading}
        >
          <Box display="flex" alignItems="center" gridGap={theme.spacing(0.6)}>
            <Typography variant="h5">
              {t("clusterDetail.voyager.planAndAssess.recommendation.heading")}
            </Typography>
            <Box>
              <YBTooltip title={t("clusterDetail.voyager.planAndAssess.recommendation.tooltip")} />
            </Box>
          </Box>
        </Box>

        <Box mb={4}>{recommendation}</Box>

        <Box display="flex">
          <Box>
            <Box mb={3}>
              <Typography variant="h5">
                {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.heading")}
              </Typography>
            </Box>

            <Grid container spacing={4}>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.noOfNodes")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {nodeCount}
                </Typography>
              </Grid>
              <Grid item xs={6} />
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.vcpuPerNode")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {vCpuPerNode}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t(
                    "clusterDetail.voyager.planAndAssess.recommendation.clusterSize.memoryPerNode"
                  )}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {memoryPerNode} GB
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t(
                    "clusterDetail.voyager.planAndAssess.recommendation.clusterSize.optimalSelectConnPerNode"
                  )}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {optimalSelectConnPerNode}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t(
                    "clusterDetail.voyager.planAndAssess.recommendation.clusterSize.optimalInsertConnPerNode"
                  )}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {optimalInsertConnPerNode}
                </Typography>
              </Grid>
            </Grid>
          </Box>

          <Divider orientation="vertical" className={classes.dividerVertical} flexItem />

          <Box>
            <Box mb={3} display="flex" justifyContent="space-between" alignItems="center">
              <Typography variant="h5">
                {t("clusterDetail.voyager.planAndAssess.recommendation.schema.heading")}
              </Typography>
              {/* <YBButton
                variant="ghost"
                startIcon={<CaretRightIcon />}
                onClick={() => setShowRecommendation(true)}
              >
                {t("clusterDetail.voyager.planAndAssess.sourceEnv.viewDetails")}
              </YBButton> */}
            </Box>

            <Grid container spacing={4}>
              <Grid item xs={6}>
                <Typography variant="h5">
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schema.colocatedTables")}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="h5">
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schema.shardedTables")}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schema.noOfTables")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {colocatedTableCount}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schema.noOfTables")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {shardedTableCount}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schema.totalSize")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {typeof colocatedTotalSize === "number"
                    ? `${convertBytesToGB(colocatedTotalSize)} GB`
                    : colocatedTotalSize}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schema.totalSize")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {typeof shardedTotalSize === "number"
                    ? `${convertBytesToGB(shardedTotalSize)} GB`
                    : shardedTotalSize}
                </Typography>
              </Grid>
            </Grid>
          </Box>
        </Box>
      </Box>

      <MigrationRecommendationSidePanel
        migration={migration}
        open={showRecommendation}
        onClose={() => setShowRecommendation(false)}
      />
    </Paper>
  );
};
