import React, { FC } from 'react';
import { makeStyles, Theme, Box, Typography, CircularProgress, Chip, IconButton } from '@material-ui/core';
import { FiberManualRecord } from '@material-ui/icons';
import { useTranslation } from 'react-i18next';
import clsx from 'clsx';
import { YBModal, GenericFailure } from '@app/components';
import { ClusterTier, useGetClusterTierSpecsQuery } from '@app/api/src';
import { formatCurrency } from '@app/helpers';
//Icons
import RocketIcon from '@app/assets/rocket-colored.svg';
import CheckIcon from '@app/assets/check.svg';
import TimesIcon from '@app/assets/times-dark.svg';

interface ClusterSpecsModalProps {
  open: boolean;
  clusterType: ClusterTier;
  onClose?: () => void;
}

const useClusterSpecsStyles = makeStyles((theme: Theme) => ({
  mainContainer: {
    padding: theme.spacing(2, 1, 3, 2)
  },
  bestForContainer: {
    border: '1px solid rgba(215, 221, 225, 0.6)',
    background: 'rgba(240, 244, 247, 0.2)',
    borderRadius: theme.spacing(1),
    display: 'flex',
    flexDirection: 'column',
    padding: theme.spacing(2)
  },
  bestForChip: {
    display: 'flex',
    flexDirection: 'row',
    width: '100px',
    height: '20px',
    padding: theme.spacing(0.25, 0.75, 0.25, 1),
    background: theme.palette.grey[100],
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.spacing(0.5),
    alignItems: 'center'
  },
  bestForTitle: {
    color: theme.palette.grey[700],
    fontWeight: 500,
    fontSize: '13px',
    marginLeft: theme.spacing(0.75)
  },
  bestForDesc: {
    color: theme.palette.grey[700],
    fontSize: '15px',
    fontWeight: 600
  },
  bullet: {
    color: theme.palette.common.indigo,
    fontSize: '13px',
    marginRight: theme.spacing(1.5)
  },
  borderBottom: {
    borderBottom: `1px solid ${theme.palette.grey[200]}`
  },
  iconCheck: {
    color: theme.palette.primary[500],
    marginRight: theme.spacing(1)
  },
  featureItem: {
    display: 'flex',
    alignItems: 'center',
    marginTop: theme.spacing(1)
  },
  modalHeader: {
    padding: theme.spacing(2, 2, 2, 5),
    borderBottom: `1px solid ${theme.palette.primary[300]}`,
    display: 'flex',
    flexDirection: 'row',
    width: '100%',
    background: theme.palette.grey[100],
    alignItems: 'center',
    justifyContent: 'space-between'
  },
  modalTitle: {
    fontWeight: 600,
    fontSize: '18px',
    color: theme.palette.grey[900],
    marginRight: theme.spacing(1)
  },
  closeBtn: {
    padding: theme.spacing(0.5),
    background: '#e1e9ee'
  },
  mr16: {
    marginRight: theme.spacing(2)
  },
  keyValRow: {
    display: 'flex',
    flexDirection: 'row',
    alignIntems: 'center',
    paddingBottom: theme.spacing(2.25),
    marginLeft: theme.spacing(2)
  },
  chip: {
    height: theme.spacing(2.5),
    borderRadius: theme.spacing(0.5),
    background: theme.palette.grey[200],
    marginLeft: theme.spacing(1),
    color: theme.palette.grey[700],
    fontSize: '11.5px'
  }
}));

export const ClusterSpecsModal: FC<ClusterSpecsModalProps> = ({ open, clusterType, onClose }) => {
  const classes = useClusterSpecsStyles();
  const { t } = useTranslation();
  const { data: tierSpecs, isError, isLoading } = useGetClusterTierSpecsQuery();
  const isFreeTier = clusterType === ClusterTier.Free;

  if (isLoading) {
    return (
      <Box display="flex" alignItems="center" justifyContent="center" mt={32}>
        <CircularProgress size={32} color="primary" />
      </Box>
    );
  }

  if (isError || !tierSpecs?.data) {
    return <GenericFailure />;
  }

  const modalTitle = (
    <Box className={classes.modalHeader}>
      <Box display="flex" flexDirection="row" alignItems="center">
        <Typography className={classes.modalTitle}>{t('clusterWizard.paidTier')}</Typography>
        <Typography variant="body2">
          {isFreeTier ? t('clusterWizard.freeTierSubtitle') : t('clusterWizard.paidTierSubtitle')}
        </Typography>
        {isFreeTier && <Chip className={classes.chip} size="small" label={t('clusterWizard.noCCRequired')} />}
      </Box>
      <IconButton size="small" className={classes.closeBtn} onClick={onClose}>
        <TimesIcon />
      </IconButton>
    </Box>
  );

  return (
    <YBModal
      size="xl"
      enableBackdropDismiss
      open={open}
      onClose={onClose}
      customTitle={modalTitle}
      overrideHeight={'auto'}
      buttonProps={{
        primary: {
          disabled: false
        }
      }}
    >
      <Box display="flex" flexDirection="column" className={classes.mainContainer}>
        <Box className={classes.bestForContainer}>
          <Box display="flex" flexDirection="row" justifyContent="space-between">
            <Box className={classes.bestForChip}>
              <RocketIcon />
              <Typography className={classes.bestForTitle}>{t('clusterWizard.bestForTitle')}</Typography>
            </Box>
            {isFreeTier && (
              <Chip className={classes.chip} size="small" label={t('clusterWizard.oneFreeTierClusterPerAccount')} />
            )}
          </Box>
          <Box mt={2}>
            {isFreeTier ? ( // Free tier best use cases
              <>
                <Box display="flex" flexDirection="row" alignItems="center">
                  <Typography className={classes.bestForDesc}>{t('clusterWizard.freeTierBestFeat1')}</Typography>
                </Box>
              </>
            ) : (
              //Paid tier best use cases
              <>
                <Box display="flex" flexDirection="row" alignItems="center">
                  <FiberManualRecord className={classes.bullet} />
                  <Typography className={classes.bestForDesc}>{t('clusterWizard.paidTierBestFeat1')}</Typography>
                </Box>
                <Box display="flex" flexDirection="row" alignItems="center" mt={1.5}>
                  <FiberManualRecord className={classes.bullet} />
                  <Typography className={classes.bestForDesc}>{t('clusterWizard.paidTierBestFeat2')}</Typography>
                </Box>
                <Box display="flex" flexDirection="row" alignItems="center" mt={1.5}>
                  <FiberManualRecord className={classes.bullet} />
                  <Typography className={classes.bestForDesc}>{t('clusterWizard.paidTierBestFeat3')}</Typography>
                </Box>
              </>
            )}
          </Box>
        </Box>
        <Box className={clsx(classes.keyValRow, classes.borderBottom)} mt={5}>
          <Typography variant="body1" className={classes.mr16}>
            {t('clusterWizard.price')}
          </Typography>
          {isFreeTier ? (
            <Typography variant="body2">{t('common.free')}</Typography>
          ) : (
            <>
              <Typography variant="body2" display="inline" color="textSecondary">
                {t('clusterWizard.startingFrom')}
              </Typography>
              &nbsp;
              <Typography variant="body2" display="inline">
                {t('clusterWizard.priceValue', {
                  value: formatCurrency(tierSpecs.data.paid_tier.per_core_cost_cents_per_hr / 100)
                })}
              </Typography>
            </>
          )}
        </Box>
        <Box className={clsx(classes.keyValRow, classes.borderBottom)} mt={2.25}>
          <Typography variant="body1" className={classes.mr16}>
            {t('clusterWizard.clusterSize')}
          </Typography>
          {isFreeTier ? (
            <>
              <Typography variant="body2" display="inline" className={classes.mr16}>
                {t('clusterWizard.freeTierNodeLimit')}
              </Typography>
              <Typography variant="body2" display="inline" className={classes.mr16}>
                {t('clusterWizard.upTo')} {'2 '} {t('clusterWizard.vCPU')}
              </Typography>
              <Typography variant="body2" display="inline" className={classes.mr16}>
                {t('units.GB', { value: 2 })} {t('clusterWizard.ram')}
              </Typography>
              <Typography variant="body2" display="inline" className={classes.mr16}>
                {t('units.GB', { value: 10 })} {t('clusterWizard.tierStorage')}
              </Typography>
            </>
          ) : (
            <Typography variant="body2">{t('clusterWizard.clusterSizePaidTier')}</Typography>
          )}
        </Box>
        <Box mt={2} mb={2} ml={2}>
          <Typography variant="body1">{t('clusterWizard.features')}</Typography>
        </Box>
        <Box display="flex" ml={2} paddingBottom={4} flexDirection="row">
          {isFreeTier ? ( // Free Tier features
            <Box display="flex" flexDirection="column">
              <div className={classes.featureItem}>
                <CheckIcon className={classes.iconCheck} />
                <Typography variant="body2">{t('clusterWizard.freeTierFeature1')}</Typography>
              </div>
              <div className={classes.featureItem}>
                <CheckIcon className={classes.iconCheck} />
                <Typography variant="body2">{t('clusterWizard.freeTierFeature2')}</Typography>
              </div>
              <div className={classes.featureItem}>
                <CheckIcon className={classes.iconCheck} />
                <Typography variant="body2">{t('clusterWizard.freeTierFeature3')}</Typography>
              </div>
              <div className={classes.featureItem}>
                <CheckIcon className={classes.iconCheck} />
                <Typography variant="body2">{t('clusterWizard.freeTierFeature4')}</Typography>
              </div>
            </Box>
          ) : (
            //Paid Tier features
            <>
              <Box display="flex" flex={5} flexDirection="column">
                <div className={classes.featureItem}>
                  <CheckIcon className={classes.iconCheck} />
                  <Typography variant="body2">{t('clusterWizard.paidTierFeature2')}</Typography>
                </div>
                <div className={classes.featureItem}>
                  <CheckIcon className={classes.iconCheck} />
                  <Typography variant="body2">{t('clusterWizard.paidTierFeature3')}</Typography>
                </div>
                <div className={classes.featureItem}>
                  <CheckIcon className={classes.iconCheck} />
                  <Typography variant="body2">{t('clusterWizard.paidTierFeature4')}</Typography>
                </div>
                <div className={classes.featureItem}>
                  <CheckIcon className={classes.iconCheck} />
                  <Typography variant="body2">{t('clusterWizard.paidTierFeature5')}</Typography>
                </div>
              </Box>
              <Box display="flex" flex={7} flexDirection="column">
                <div className={classes.featureItem}>
                  <CheckIcon className={classes.iconCheck} />
                  <Typography variant="body2">{t('clusterWizard.paidTierFeature6')}</Typography>
                </div>
                <div className={classes.featureItem}>
                  <CheckIcon className={classes.iconCheck} />
                  <Typography variant="body2">{t('clusterWizard.paidTierFeature7')}</Typography>
                </div>
                <div className={classes.featureItem}>
                  <CheckIcon className={classes.iconCheck} />
                  <Typography variant="body2">{t('clusterWizard.paidTierFeature8')}</Typography>
                </div>
                <div className={classes.featureItem}>
                  <CheckIcon className={classes.iconCheck} />
                  <Typography variant="body2">{t('clusterWizard.paidTierFeature9')}</Typography>
                </div>
              </Box>
            </>
          )}
        </Box>
      </Box>
    </YBModal>
  );
};
