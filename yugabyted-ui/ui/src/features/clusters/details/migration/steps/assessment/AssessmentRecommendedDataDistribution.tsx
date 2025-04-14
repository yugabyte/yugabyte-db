import React, { FC } from "react";
import { Box, Divider, Grid, Typography, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBAccordion, YBButton } from "@app/components";
import { MigrationRecommendationSidePanel } from "./AssessmentRecommendationSidePanel";
import type { Migration } from "../../MigrationOverview";
import { getMemorySizeUnits } from "@app/helpers";
import CaretRightIcon from "@app/assets/caret-right-circle.svg";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    textAlign: "left",
  },
  dividerVertical: {
    marginLeft: theme.spacing(5),
    marginRight: theme.spacing(5),
  },
  value: {
    paddingTop: theme.spacing(0.36),
    textAlign: "start",
  },
  recommendationCard: {
    display: "flex",
    flexDirection: "row",
    justifyContent: "space-between",
    width: "100%",
    paddingRight: theme.spacing(2),
    paddingLeft: theme.spacing(2),
    paddingTop: theme.spacing(2),
    paddingBottom: theme.spacing(2),
  },
}));

interface RecommendedDataDistributionProps {
  migration: Migration | undefined;
  colocatedTableCount: string | number;
  shardedTableCount: string | number;
  colocatedTotalSize: string | number;
  shardedTotalSize: string | number;
}

export const RecommendedDataDistribution: FC<RecommendedDataDistributionProps> = ({
  migration,
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
    <>
      <YBAccordion
        titleContent={
          <Box display="flex" alignItems="center" gridGap={theme.spacing(3)}>
            {t("clusterDetail.voyager.planAndAssess.recommendation.dataDistribution.heading")}
            <YBButton
              variant="ghost"
              startIcon={<CaretRightIcon />}
              onClick={(e: any) => {
                setShowRecommendation(true);
                e.stopPropagation();
              }}
            >
              {t("clusterDetail.voyager.planAndAssess.sourceEnv.viewDetails")}
            </YBButton>
          </Box>}
        defaultExpanded
        contentSeparator
      >
        <Box className={classes.recommendationCard}>
          <Grid container spacing={4}>
            <Grid item xs={12}>
              <Typography variant="h5">
                {t("clusterDetail.voyager.planAndAssess.recommendation.dataDistribution.colocated")}
              </Typography>
            </Grid>
            <Grid item xs={6}>
              <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.dataDistribution.tables")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {colocatedTableCount}
                </Typography>
              </Box>
            </Grid>
            {colocatedTotalSize ? (
              <Grid item xs={6}>
                <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
                  <Typography variant="subtitle2" className={classes.label}>
                    {t("clusterDetail.voyager.planAndAssess.recommendation.dataDistribution.size")}
                  </Typography>
                  <Typography variant="body2" className={classes.value}>
                    {typeof colocatedTotalSize === "number"
                      ? getMemorySizeUnits(colocatedTotalSize)
                      : colocatedTotalSize}
                  </Typography>
                </Box>
              </Grid>
            ) : null}
          </Grid>
          <Divider orientation="vertical" className={classes.dividerVertical} flexItem />
          <Grid container spacing={4}>
            <Grid item xs={12}>
              <Typography variant="h5">
                {t("clusterDetail.voyager.planAndAssess.recommendation.dataDistribution.sharded")}
              </Typography>
            </Grid>
            <Grid item xs={6}>
              <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
                <Typography variant="subtitle2" className={classes.label}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.dataDistribution.tables")}
                </Typography>
                <Typography variant="body2" className={classes.value}>
                  {shardedTableCount}
                </Typography>
              </Box>
            </Grid>
            {shardedTotalSize ? (
              <Grid item xs={6}>
                <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
                  <Typography variant="subtitle2" className={classes.label}>
                    {t("clusterDetail.voyager.planAndAssess.recommendation.dataDistribution.size")}
                  </Typography>
                  <Typography variant="body2" className={classes.value}>
                    {typeof shardedTotalSize === "number"
                      ? getMemorySizeUnits(shardedTotalSize)
                      : shardedTotalSize}
                  </Typography>
                </Box>
              </Grid>
            ) : null}
          </Grid>
        </Box>
      </YBAccordion>
      <MigrationRecommendationSidePanel
        migration={migration}
        open={showRecommendation}
        onClose={() => setShowRecommendation(false)}
      />
    </>
  );
};
