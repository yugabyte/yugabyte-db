import React, { FC } from "react";
import { Box, Typography, makeStyles } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBAccordion } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import { MetadataItem } from "../../components/MetadataItem";
import HoneycombIcon from "@app/assets/honeycomb.svg";
import HexagonIcon from "@app/assets/hexagon.svg";
import { useGetClusterQuery } from '@app/api/src/apis/ClusterApi';
import { ComparisonModal } from "./ComparisonModal";

const useStyles = makeStyles((theme) => ({
  recommendationCard: {
    display: "flex",
    flexDirection: "row",
    width: "100%",
    padding: theme.spacing(2),
    gap: theme.spacing(11.5),
  },
  icon: {
    height: theme.spacing(2),
    width: theme.spacing(2),
    display: "flex",
    justifyContent: "center",
    alignItems: "center",
    flexShrink: 0,
  },
  leftSection: {
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(2),
    flexShrink: 0,
    width: "100%",
    maxWidth: theme.spacing(50)
  },
  rightSection: {
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(1),
    backgroundColor: theme.palette.background.default,
    border: `1px solid ${theme.palette.divider}`,
    borderRadius: theme.spacing(1),
    padding: theme.spacing(2),
    width: theme.spacing(41.25),
    maxWidth: theme.spacing(41.25)
  },
  nodeSection: {
    display: "flex",
    flexDirection: "row",
    gap: theme.spacing(1),
    alignItems: "flex-start",
  },
  nodeContent: {
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(1),
    minWidth: 0,
  },
  metadataGrid: {
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(1),
  },
  totalNodeContent: {
    display: "flex",
    flexDirection: "column",
    gap: theme.spacing(1),
  },
  sectionTitle: {
    color: theme.palette.text.primary,
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.fontSize,
    fontWeight: theme.typography.body2.fontWeight,
    lineHeight: "16px",
    marginBottom: theme.spacing(1),
  },
  nodeCountValue: {
    color: theme.palette.text.primary,
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.fontSize,
    fontWeight: theme.typography.body2.fontWeight,
    lineHeight: "16px",
  },
  labelStyleText: {
    color: theme.palette.text.primary,
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.fontSize,
    fontStyle: 'normal',
    fontWeight: theme.typography.body2.fontWeight,
    lineHeight: '32px'
  },
  badgeMargin: {
    marginLeft: theme.spacing(0.5)
  },
  infoBar: {
    borderRadius: theme.shape.borderRadius,
    background: 'var(--Success-Success-050, #F0FAF6)',
    display: 'flex',
    padding: theme.spacing(1.5),
    alignItems: 'center',
    gap: theme.spacing(0.5),
    alignSelf: 'stretch',
    width: '100%',
    cursor: 'pointer',
    marginTop: theme.spacing(2)
  },
  warningBar: {
    borderRadius: theme.shape.borderRadius,
    background: 'var(--Warning-Warning-050, #FFF7E6)',
    display: 'flex',
    padding: theme.spacing(1.5),
    alignItems: 'center',
    gap: theme.spacing(0.5),
    alignSelf: 'stretch',
    width: '100%',
    marginTop: theme.spacing(2)
  },
  warningTextBold: {
    color: theme.palette.warning[900],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.fontSize,
    fontStyle: 'normal',
    fontWeight: theme.typography.h1.fontWeight,
    lineHeight: '16px'
  },
  warningText: {
    color: theme.palette.warning[900],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.fontSize,
    fontStyle: 'normal',
    fontWeight: theme.typography.button.fontWeight,
    lineHeight: '16px'
  },
  compareLink: {
    color: theme.palette.primary[600],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.fontSize,
    fontStyle: 'normal',
    fontWeight: theme.typography.button.fontWeight,
    lineHeight: '16px',
    textDecorationLine: 'underline',
    textDecorationStyle: 'solid',
    textDecorationSkipInk: 'none',
    textDecorationThickness: 'auto',
    textUnderlineOffset: 'auto',
    textUnderlinePosition: 'from-font',
    cursor: 'pointer'
  },
  successText: {
    color: theme.palette.grey[700],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.fontSize,
    fontStyle: 'normal',
    fontWeight: theme.typography.button.fontWeight,
    lineHeight: '16px'
  },
  successLink: {
    color: theme.palette.primary[600],
    fontFamily: theme.typography.fontFamily,
    fontSize: theme.typography.fontSize,
    fontStyle: 'normal',
    fontWeight: theme.typography.button.fontWeight,
    lineHeight: '16px',
    textDecorationLine: 'underline',
    textDecorationStyle: 'solid',
    textDecorationSkipInk: 'none',
    textDecorationThickness: 'auto',
    textUnderlineOffset: 'auto',
    textUnderlinePosition: 'from-font',
    cursor: 'pointer'
  }
}));

interface RecommendedClusterSizeProps {
  recommendedNodeCount: string | number;
  recommendedVCpuPerNode: string | number;
  recommendedMemoryPerNode: string | number;
  recommendedOptimalSelectConnPerNode: string | number;
  recommendedOptimalInsertConnPerNode: string | number;
}

export const RecommendedClusterSize: FC<RecommendedClusterSizeProps> = ({
  recommendedNodeCount,
  recommendedVCpuPerNode,
  recommendedMemoryPerNode,
  recommendedOptimalSelectConnPerNode,
  recommendedOptimalInsertConnPerNode,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();
  const [showCompareModal, setShowCompareModal] = React.useState(false);

  // Fetch cluster info from API
  const { data: clusterData, isLoading, isError } = useGetClusterQuery();

  // Extract relevant info from API response
  const currentClusterInfo = clusterData?.data?.spec?.cluster_info;
  const currentNodeInfo = currentClusterInfo?.node_info;
  const currentNodeCount = currentClusterInfo?.num_nodes ?? '-';
  const currentVCpuPerNode = currentNodeInfo?.num_cores ?? '-';
  const currentMemoryPerNode = currentNodeInfo?.ram_provisioned_gb ?? '-';
  // You can add more fields as needed

  if (isLoading) {
    return <Typography>Loading cluster info...</Typography>;
  }
  if (isError) {
    return <Typography>Error loading cluster info.</Typography>;
  }

  const comparisonData = [
    {
      label: 'Number of Nodes',
      recommended: Number(recommendedNodeCount),
      current: Number(currentNodeCount),
      status: Number(currentNodeCount) < Number(recommendedNodeCount)
        ? 'warning'
        : 'ok'
    },
    {
      label: 'vCPU',
      recommended: Number(recommendedVCpuPerNode),
      current: Number(currentVCpuPerNode),
      status: Number(currentVCpuPerNode) < Number(recommendedVCpuPerNode)
        ? 'warning'
        : 'ok'
    },
    {
      label: 'Memory',
      recommended: Number(recommendedMemoryPerNode),
      current: Number(currentMemoryPerNode),
      status: Number(currentMemoryPerNode) < Number(recommendedMemoryPerNode)
        ? 'warning'
        : 'ok'
    },
    {
      label: 'Total vCPU',
      recommended: Number(recommendedNodeCount) * Number(recommendedVCpuPerNode),
      current: Number(currentNodeCount) * Number(currentVCpuPerNode),
      status: (Number(currentNodeCount) * Number(currentVCpuPerNode)) <
        (Number(recommendedNodeCount) * Number(recommendedVCpuPerNode))
        ? 'warning'
        : 'ok'
    },
    {
      label: 'Total Memory',
      recommended: Number(recommendedNodeCount) * Number(recommendedMemoryPerNode),
      current: Number(currentNodeCount) * Number(currentMemoryPerNode),
      status: (Number(currentNodeCount) * Number(currentMemoryPerNode)) <
        (Number(recommendedNodeCount) * Number(recommendedMemoryPerNode))
        ? 'warning'
        : 'ok'
    }
  ];

  const warningCount = comparisonData.filter(item => item.status === 'warning').length;

  // Handler for opening modal
  const handleOpenCompareModal = () => setShowCompareModal(true);

  return (
    <YBAccordion
      titleContent={
        <>
          {t('clusterDetail.voyager.planAndAssess.recommendation.clusterSize.heading')}
          {' '}
          <YBBadge
            variant={warningCount > 0 ? BadgeVariant.Warning : BadgeVariant.Success}
            className={classes.badgeMargin}
            noText={true}
          />
        </>
      }
      defaultExpanded
      contentSeparator
    >
      <Box display="flex" flexDirection="column" width="100%" p={0}>
        <Box className={classes.recommendationCard}>
          {/* Left Section */}
          <Box className={classes.leftSection}>
            {/* Number of Nodes Section */}
            <Box className={classes.nodeSection} style={{ marginTop: 12 }}>
              <Box className={classes.icon}>
                <HoneycombIcon />
              </Box>
              <Box className={classes.nodeContent}>
                <Typography className={classes.sectionTitle}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.noOfNodes")}
                </Typography>
                <Typography className={classes.nodeCountValue}>
                  {recommendedNodeCount}
                </Typography>
              </Box>
            </Box>

            {/* Node Size Section */}
            <Box className={classes.nodeSection}>
              <Box className={classes.icon}>
                <HexagonIcon />
              </Box>
              <Box className={classes.nodeContent}>
                <Typography className={classes.sectionTitle}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.nodeSize")}
                </Typography>
                <Box className={classes.metadataGrid}>
                  <MetadataItem
                    layout="horizontal"
                    label={[
                      t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.vcpu"),
                      t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.memory"),
                      t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                        "optimalSelectConn"),
                      t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                        "optimalInsertConn")
                    ]}
                    value={[
                      recommendedVCpuPerNode,
                      `${recommendedMemoryPerNode} GB`,
                      recommendedOptimalSelectConnPerNode,
                      recommendedOptimalInsertConnPerNode
                    ]}
                  />
                </Box>
              </Box>
            </Box>
          </Box>

          <Box className={classes.rightSection}>
            <Box className={classes.nodeSection}>
              <Box className={classes.icon}>
                <HoneycombIcon />
              </Box>
              <Box className={classes.nodeContent}>
                <Typography className={classes.sectionTitle}>
                  {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                    "totalNodeSize")}
                </Typography>
                <Box className={classes.totalNodeContent}>
                  <MetadataItem
                    layout="vertical"
                    label={[
                      t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.vcpu"),
                      t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.memory"),
                      t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                        "noOfConnections")
                    ]}
                    value={[
                      isNaN(Number(recommendedVCpuPerNode) * Number(recommendedNodeCount))
                        ? t("common.notAvailable")
                        : Number(recommendedVCpuPerNode) * Number(recommendedNodeCount),
                      isNaN(Number(recommendedMemoryPerNode) * Number(recommendedNodeCount))
                        ? t("common.notAvailable")
                        : Number(recommendedMemoryPerNode) * Number(recommendedNodeCount) + " GB",
                      isNaN(
                        (Number(recommendedOptimalInsertConnPerNode) +
                         Number(recommendedOptimalSelectConnPerNode)) *
                          Number(recommendedNodeCount)
                      )
                        ? t("common.notAvailable")
                        : (Number(recommendedOptimalInsertConnPerNode) +
                           Number(recommendedOptimalSelectConnPerNode)) *
                            Number(recommendedNodeCount)
                    ]}
                  />
                </Box>
              </Box>
            </Box>
          </Box>
        </Box>
        <Box
          className={warningCount > 0 ? classes.warningBar : classes.infoBar}
        >
          {warningCount > 0 ? (
            <>
              <YBBadge
                variant={BadgeVariant.Warning}
                className={classes.badgeMargin}
                noText={true}
              />
              <Typography className={classes.warningTextBold}>
                {t('clusterDetail.voyager.planAndAssess.recommendation.clusterSize.' +
                  "throughputNotAchieved")}
              </Typography>
              <Typography className={classes.warningText}>
                {t('clusterDetail.voyager.planAndAssess.recommendation.clusterSize.' +
                  "withCurrentCluster")}
              </Typography>
              <Typography className={classes.compareLink} onClick={handleOpenCompareModal}>
                {t('clusterDetail.voyager.planAndAssess.recommendation.clusterSize.' +
                  "compareDifferences")}
              </Typography>
            </>
          ) : (
            <>
              <YBBadge
                variant={BadgeVariant.Success}
                className={classes.badgeMargin}
                noText={true}
              />
              <Typography className={classes.successText}>
                {t('clusterDetail.voyager.planAndAssess.recommendation.clusterSize.successPrefix')}
                <Typography component="span" className={classes.successLink}>
                  {t('clusterDetail.voyager.planAndAssess.recommendation.clusterSize.' +
                    "currentCluster")}
                </Typography>{' '}
                {t('clusterDetail.voyager.planAndAssess.recommendation.clusterSize.successSuffix')}
              </Typography>
            </>
          )}
        </Box>
      </Box>

      {/* Compare Differences Modal */}
      <ComparisonModal
        open={showCompareModal}
        onClose={() => setShowCompareModal(false)}
        recommendedNodeCount={recommendedNodeCount}
        recommendedVCpuPerNode={recommendedVCpuPerNode}
        recommendedMemoryPerNode={recommendedMemoryPerNode}
        recommendedOptimalSelectConnPerNode={recommendedOptimalSelectConnPerNode}
        recommendedOptimalInsertConnPerNode={recommendedOptimalInsertConnPerNode}
        currentNodeCount={currentNodeCount}
        currentVCpuPerNode={currentVCpuPerNode}
        currentMemoryPerNode={currentMemoryPerNode}
      />
    </YBAccordion>
  );
};
