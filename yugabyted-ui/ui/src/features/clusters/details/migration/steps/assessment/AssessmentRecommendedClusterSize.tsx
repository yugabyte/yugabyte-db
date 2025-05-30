import React, { FC } from "react";
import { Box, Grid, Typography, makeStyles, useTheme } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBAccordion } from "@app/components";
import HoneycombIcon from "@app/assets/honeycomb.svg";
import HexagonIcon from "@app/assets/hexagon.svg";

const useStyles = makeStyles((theme) => ({
  label: {
    color: theme.palette.grey[500],
    fontWeight: theme.typography.fontWeightMedium as number,
    textTransform: "uppercase",
    textAlign: "left",
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
  totalNodeSize: {
    display: "flex",
    flexDirection: "column",
    gridGap: theme.spacing(2),
    backgroundColor: theme.palette.info[400],
    height: "100%",
    paddingRight: theme.spacing(1),
    paddingLeft: theme.spacing(1),
    paddingTop: theme.spacing(1),
    paddingBottom: theme.spacing(1),
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
  icon: {
    height: "16px",
    width: "16px",
    display: "flex",
    justifyContent: "center",
    alignItems: "center",
  },
}));

interface RecommendedClusterSizeProps {
  nodeCount: string | number;
  vCpuPerNode: string | number;
  memoryPerNode: string | number;
  optimalSelectConnPerNode: string | number;
  optimalInsertConnPerNode: string | number;
}

export const RecommendedClusterSize: FC<RecommendedClusterSizeProps> = ({
  nodeCount,
  vCpuPerNode,
  memoryPerNode,
  optimalSelectConnPerNode,
  optimalInsertConnPerNode,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const theme = useTheme();

  return (
    <YBAccordion
      titleContent=
        {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.heading")}
      defaultExpanded
      contentSeparator
    >
      <Box className={classes.recommendationCard}>
        <Box display="flex" flexDirection="row" justifyContent="space-between">
          <Box display="flex" flexDirection="column" gridGap={theme.spacing(5)}>
            <Box display="flex" flexDirection="row" gridGap={theme.spacing(1)}>
              <Box className={classes.icon}>
                <HoneycombIcon/>
              </Box>
              <Box display="flex" flexDirection="column" gridGap={theme.spacing(1)}>
                <Typography variant="body1">
                  {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                      "noOfNodes")}
                </Typography>
                {nodeCount}
              </Box>
            </Box>
            <Box display="flex" flexDirection="row" gridGap={theme.spacing(1)}>
              <Box className={classes.icon}>
                <HexagonIcon/>
              </Box>
              <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
                <Typography variant="body1">
                  {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                      "nodeSize")}
                </Typography>
                <Grid container spacing={1}>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                          "vcpu")}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    {vCpuPerNode}
                  </Grid>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                          "memory")}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    {memoryPerNode} GB
                  </Grid>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                          "optimalSelectConn")}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    {optimalSelectConnPerNode}
                  </Grid>
                  <Grid item xs={6}>
                    <Typography variant="subtitle2" className={classes.label}>
                      {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                          "optimalInsertConn")}
                    </Typography>
                  </Grid>
                  <Grid item xs={6}>
                    {optimalInsertConnPerNode}
                  </Grid>
                </Grid>
              </Box>
            </Box>
          </Box>
        </Box>
        <Box display="flex" flexDirection="column" className={classes.boxBody}>
          <Box display="flex" flexDirection="row" gridGap={theme.spacing(1)}>
            <Box className={classes.icon}>
              <HoneycombIcon/>
            </Box>
            <Box display="flex" flexDirection="column" gridGap={theme.spacing(3)}>
              <Typography variant="body1">
                {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                    "totalNodeSize")}
              </Typography>
              <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)}>
                <Box display="flex" flexDirection="column" gridGap={theme.spacing(0.5)}>
                  <Typography variant="subtitle2" className={classes.label}>
                    {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.vcpu")}
                  </Typography>
                  {isNaN(Number(vCpuPerNode) * Number(nodeCount))
                    ? t("common.notAvailable")
                    : Number(vCpuPerNode) * Number(nodeCount)}
                </Box>
                <Box display="flex" flexDirection="column" gridGap={theme.spacing(0.5)}>
                  <Typography variant="subtitle2" className={classes.label}>
                    {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                        "memory")}
                  </Typography>
                  {isNaN(Number(memoryPerNode) * Number(nodeCount))
                    ? t("common.notAvailable")
                    : Number(memoryPerNode) * Number(nodeCount) + " GB"}
                </Box>
                <Box display="flex" flexDirection="column" gridGap={theme.spacing(0.5)}>
                  <Typography variant="subtitle2" className={classes.label}>
                    {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                        "noOfConnections")}
                  </Typography>
                {isNaN(
                  (Number(optimalInsertConnPerNode) + Number(optimalSelectConnPerNode)) *
                   Number(nodeCount))
                  ? t("common.notAvailable")
                  : (Number(optimalInsertConnPerNode) + Number(optimalSelectConnPerNode)) *
                    Number(nodeCount)}
                </Box>
              </Box>
            </Box>
          </Box>
        </Box>
      </Box>
    </YBAccordion>
  );
};
