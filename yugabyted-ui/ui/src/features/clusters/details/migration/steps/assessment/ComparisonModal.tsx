import React, { FC } from "react";
import { Box, Typography, makeStyles, Divider } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { YBModal } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";
import ArrowRightIcon from "@app/assets/arrow-right.svg";

const useStyles = makeStyles((theme) => ({
  // Modal Styles
  modalContainer: {
    padding: '20px',
    backgroundColor: '#F7F9FB',
    borderRadius: '0 0 8px 8px', // Only bottom corners rounded
    border: '1px solid #E9EEF2',
    height: 'calc(100% - 64px)', // Subtract header height
    boxSizing: 'border-box',
    width: '100%',
    margin: 0,
    position: 'absolute',
    bottom: 0,
    left: 0,
    right: 0
  },
  modalContentOverride: {
    '& .MuiDialogContent-root': {
      padding: 0
    }
  },
  modalContent: {
    display: 'flex',
    flexDirection: 'row',
    gap: theme.spacing(2),
    backgroundColor: '#fff',
    borderRadius: '8px',
    width: '100%',
    height: '100%',
    minHeight: '100%'
  },
  modalLeftColumn: {
    width: 'fit-content',
    height: '327px',
    padding: '52px 16px 16px 16px',
    display: 'flex',
    flexDirection: 'column',
    boxSizing: 'border-box'
  },
  modalColumnTitle: {
    color: 'var(--Text-Color-Grey-900, #0B1117)',
    fontFamily: 'Inter',
    fontSize: '13px',
    fontStyle: 'normal',
    fontWeight: 600,
    lineHeight: '32px'
  },
  modalColumnTitleShort: {
    color: 'var(--Text-Color-Grey-900, #0B1117)',
    fontFamily: 'Inter',
    fontSize: '13px',
    fontStyle: 'normal',
    fontWeight: 600,
    lineHeight: '16px',
    marginTop: '12px'
  },
  modalSubRow: {
    marginLeft: theme.spacing(2),
    display: 'flex',
    alignItems: 'center',
  },
  modalArrow: {
    marginRight: theme.spacing(1)
  },
  modalDivider: {
    height: '310px',
    margin: '52px 16px',
    borderColor: '#E9EEF2'
  },
  modalRightColumn: {
    paddingTop: '25px',
    display: 'flex',
    flexDirection: 'column',
    gap: 0
  },
  modalCurrentColumn: {
    marginLeft: 70,
    paddingTop: '25px',
    display: 'flex',
    flexDirection: 'column',
    gap: 0,
  },
  modalValueRow: {
    display: 'flex',
    alignItems: 'center'
  },
  modalValueRowSpaced: {
    display: 'flex',
    alignItems: 'center',
    marginTop: 32
  },
  modalValueRowTotal: {
    display: 'flex',
    alignItems: 'center',
    marginTop: 15
  },
  modalValueText: {
    flex: 1
  },
  badgeContainer: {
    marginLeft: 'auto'
  },
  labelStyleText: {
    color: 'var(--Text-Color-Grey-900, #0B1117)',
    fontFamily: 'Inter',
    fontSize: '13px',
    fontStyle: 'normal',
    fontWeight: 400,
    lineHeight: '32px'
  },
  badgeMargin: {
    marginLeft: theme.spacing(2)
  }
}));

interface ComparisonModalProps {
  open: boolean;
  onClose: () => void;
  recommendedNodeCount: string | number;
  recommendedVCpuPerNode: string | number;
  recommendedMemoryPerNode: string | number;
  recommendedOptimalSelectConnPerNode: string | number;
  recommendedOptimalInsertConnPerNode: string | number;
  currentNodeCount: string | number;
  currentVCpuPerNode: string | number;
  currentMemoryPerNode: string | number;
}

const formatGB = (value: string | number) => {
  return typeof value === 'number' ? `${value.toFixed(1)} GB` : `${value} GB`;
};

export const ComparisonModal: FC<ComparisonModalProps> = ({
  open,
  onClose,
  recommendedNodeCount,
  recommendedVCpuPerNode,
  recommendedMemoryPerNode,
  recommendedOptimalSelectConnPerNode,
  recommendedOptimalInsertConnPerNode,
  currentNodeCount,
  currentVCpuPerNode,
  currentMemoryPerNode,
}) => {
  const classes = useStyles();
  const { t } = useTranslation();

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
        "comparisonModalTitle")}
      size="md"
      overrideHeight={500}
      enableBackdropDismiss={true}
      classes={{ paper: classes.modalContentOverride }}
    >
      <Box className={classes.modalContainer}>
        <Box className={classes.modalContent}>
          <Box className={classes.modalLeftColumn}>
            <Typography variant="h6" className={classes.modalColumnTitle}>
              {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.noOfNodes")}
            </Typography>
            <Typography variant="h6" className={classes.modalColumnTitle}>
              {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.nodeConfig")}
            </Typography>
            <Box className={classes.modalSubRow}>
              <ArrowRightIcon className={classes.labelStyleText} />
              <Typography className={classes.labelStyleText}>vCPU</Typography>
            </Box>
            <Box className={classes.modalSubRow}>
              <ArrowRightIcon className={classes.labelStyleText} />
              <Typography className={classes.labelStyleText}>Memory</Typography>
            </Box>
            <Box className={classes.modalSubRow}>
              <ArrowRightIcon className={classes.labelStyleText} />
              <Typography className={classes.labelStyleText} noWrap>
                Optimal SELECT Connections
              </Typography>
            </Box>
            <Box className={classes.modalSubRow}>
              <ArrowRightIcon className={classes.labelStyleText} />
              <Typography className={classes.labelStyleText}>
                Optimal INSERT Connections
              </Typography>
            </Box>
            <Typography>Total</Typography>
            <Box className={classes.modalSubRow}>
              <ArrowRightIcon className={classes.modalArrow} />
              <Typography className={classes.labelStyleText}>vCPU</Typography>
            </Box>
            <Box className={classes.modalSubRow}>
              <ArrowRightIcon className={classes.modalArrow} />
              <Typography className={classes.labelStyleText}>Memory</Typography>
            </Box>
            <Box className={classes.modalSubRow}>
              <ArrowRightIcon className={classes.modalArrow} />
              <Typography className={classes.labelStyleText}>
                Number of Connections
              </Typography>
            </Box>
          </Box>
          <Divider orientation="vertical" flexItem className={classes.modalDivider} />
          <Box className={classes.modalRightColumn}>
            <Typography variant="h6" className={classes.modalColumnTitleShort}>
              {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize." +
                "recommendedCluster")}
            </Typography>
            <Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {recommendedNodeCount}
                </Typography>
              </Box>
              <Box className={classes.modalValueRowSpaced}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {recommendedVCpuPerNode}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {formatGB(recommendedMemoryPerNode)}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {recommendedOptimalSelectConnPerNode}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {recommendedOptimalInsertConnPerNode}
                </Typography>
              </Box>
              <Box className={classes.modalValueRowTotal}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {/* Total heading, no value */}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {recommendedNodeCount &&
                   recommendedVCpuPerNode &&
                   !isNaN(Number(recommendedNodeCount)) &&
                   !isNaN(Number(recommendedVCpuPerNode))
                    ? Number(recommendedNodeCount) * Number(recommendedVCpuPerNode)
                    : '-'}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {recommendedNodeCount &&
                   recommendedMemoryPerNode &&
                   !isNaN(Number(recommendedNodeCount)) &&
                   !isNaN(Number(recommendedMemoryPerNode))
                    ? formatGB(Number(recommendedNodeCount) * Number(recommendedMemoryPerNode))
                    : '-'}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {Number(recommendedOptimalInsertConnPerNode) +
                   Number(recommendedOptimalSelectConnPerNode)}
                </Typography>
              </Box>
            </Box>
          </Box>
          <Box className={classes.modalCurrentColumn}>
            <Typography variant="h6" className={classes.modalColumnTitleShort}>
              {t("clusterDetail.voyager.planAndAssess.recommendation.clusterSize.currentCluster")}
            </Typography>
            <Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {currentNodeCount}
                </Typography>
                <Typography className={classes.badgeContainer}>
                  {Number(currentNodeCount) < Number(recommendedNodeCount) ? (
                    <YBBadge
                      variant={BadgeVariant.Warning}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  ) : (
                    <YBBadge
                      variant={BadgeVariant.Success}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  )}
                </Typography>
              </Box>
              <Box className={classes.modalValueRowSpaced}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {currentVCpuPerNode}
                </Typography>
                <Typography className={classes.badgeContainer}>
                  {Number(currentVCpuPerNode) < Number(recommendedVCpuPerNode) ? (
                    <YBBadge
                      variant={BadgeVariant.Warning}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  ) : (
                    <YBBadge
                      variant={BadgeVariant.Success}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  )}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {currentMemoryPerNode &&
                   currentNodeCount &&
                   !isNaN(Number(currentMemoryPerNode)) &&
                   !isNaN(Number(currentNodeCount))
                    ? formatGB(Number(currentMemoryPerNode) * Number(currentNodeCount))
                    : '-'}
                </Typography>
                <Typography className={classes.badgeContainer}>
                  {Number(currentMemoryPerNode) * Number(currentNodeCount) <
                   Number(recommendedMemoryPerNode) * Number(recommendedNodeCount) ? (
                    <YBBadge
                      variant={BadgeVariant.Warning}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  ) : (
                    <YBBadge
                      variant={BadgeVariant.Success}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  )}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  -
                </Typography>
                <Typography className={classes.badgeContainer}>
                  <YBBadge
                    variant={BadgeVariant.Warning}
                    className={classes.badgeMargin}
                    noText={true}
                  />
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  -
                </Typography>
                <Typography className={classes.badgeContainer}>
                  <YBBadge
                    variant={BadgeVariant.Warning}
                    className={classes.badgeMargin}
                    noText={true}
                  />
                </Typography>
              </Box>
              <Box className={classes.modalValueRowTotal}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {/* Total heading, no value */}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {currentVCpuPerNode &&
                   currentNodeCount &&
                   !isNaN(Number(currentNodeCount)) &&
                   !isNaN(Number(currentVCpuPerNode))
                    ? Number(currentVCpuPerNode) * Number(currentNodeCount)
                    : '-'}
                </Typography>
                <Typography className={classes.badgeContainer}>
                  {Number(currentVCpuPerNode) * Number(currentNodeCount) <
                   Number(recommendedVCpuPerNode) * Number(recommendedNodeCount) ? (
                    <YBBadge
                      variant={BadgeVariant.Warning}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  ) : (
                    <YBBadge
                      variant={BadgeVariant.Success}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  )}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  {currentMemoryPerNode &&
                   currentNodeCount &&
                   !isNaN(Number(currentMemoryPerNode)) &&
                   !isNaN(Number(currentNodeCount))
                    ? formatGB(Number(currentMemoryPerNode) * Number(currentNodeCount))
                    : '-'}
                </Typography>
                <Typography className={classes.badgeContainer}>
                  {Number(currentMemoryPerNode) * Number(currentNodeCount) <
                   Number(recommendedMemoryPerNode) * Number(recommendedNodeCount) ? (
                    <YBBadge
                      variant={BadgeVariant.Warning}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  ) : (
                    <YBBadge
                      variant={BadgeVariant.Success}
                      className={classes.badgeMargin}
                      noText={true}
                    />
                  )}
                </Typography>
              </Box>
              <Box className={classes.modalValueRow}>
                <Typography className={`${classes.labelStyleText} ${classes.modalValueText}`}>
                  -
                </Typography>
                <Typography className={classes.badgeContainer}>
                  <YBBadge
                    variant={BadgeVariant.Warning}
                    className={classes.badgeMargin}
                    noText={true}
                  />
                </Typography>
              </Box>
            </Box>
          </Box>
        </Box>
      </Box>
    </YBModal>
  );
};
