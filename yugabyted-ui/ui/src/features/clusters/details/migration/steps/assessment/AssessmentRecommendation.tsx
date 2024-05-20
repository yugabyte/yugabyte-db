import React, { FC } from "react";
import { Box, Divider, Grid, Paper, Typography, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBButton, YBTooltip } from "@app/components";
import CaretRightIcon from "@app/assets/Drilldown.svg";
import { MigrationSchemaRecommendation } from "./AssessmentSchemaRecommend";

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
  description: string;
  clusterSize: {
    nodeCount: string;
    vcpuPerNode: string;
    memoryPerNode: string;
    optSelectConnPerNode: string;
    optInsertConnPerNode: string;
  };
  schemaRecommendation: {
    colocatedTables: string;
    colocatedSize: string;
    shardedTables: string;
    shardedSize: string;
  };
}

export const MigrationAssessmentRecommendation: FC<MigrationAssessmentRecommendationProps> = ({
  description,
  clusterSize,
  schemaRecommendation,
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

        <Box mb={4}>{description}</Box>

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
                  {clusterSize.nodeCount}
                </Typography>
              </Grid>
              <Grid item xs={6} />
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.vcpuPerNode")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {clusterSize.vcpuPerNode}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t(
                    "clusterDetail.voyager.planAndAssess.recommendation.clusterSize.memoryPerNode"
                  )}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {clusterSize.memoryPerNode}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t(
                    "clusterDetail.voyager.planAndAssess.recommendation.clusterSize.optimalSelectConnPerNode"
                  )}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {clusterSize.optSelectConnPerNode}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t(
                    "clusterDetail.voyager.planAndAssess.recommendation.clusterSize.optimalInsertConnPerNode"
                  )}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {clusterSize.optInsertConnPerNode}
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
              <YBButton
                variant="ghost"
                startIcon={<CaretRightIcon />}
                onClick={() => setShowRecommendation(true)}
              >
                {t("clusterDetail.voyager.planAndAssess.sourceEnv.viewDetails")}
              </YBButton>
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
                  {schemaRecommendation.colocatedTables}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schema.noOfTables")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {schemaRecommendation.shardedTables}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schema.totalSize")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {schemaRecommendation.colocatedSize}
                </Typography>
              </Grid>
              <Grid item xs={6}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.schema.totalSize")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {schemaRecommendation.shardedSize}
                </Typography>
              </Grid>
            </Grid>
          </Box>
        </Box>
      </Box>

      <MigrationSchemaRecommendation
        open={showRecommendation}
        onClose={() => setShowRecommendation(false)}
      />
    </Paper>
  );
};
