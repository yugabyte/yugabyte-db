import { useState } from 'react';
import clsx from 'clsx';
import { ArrowDropDown } from '@material-ui/icons';
import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';

import { FailoverType } from '../constants';
import { ReactComponent as InfoIcon } from '../../../../redesign/assets/info-message.svg';
import { ReactComponent as SelectedIcon } from '../../../../redesign/assets/circle-selected.svg';
import { ReactComponent as UnselectedIcon } from '../../../../redesign/assets/circle-empty.svg';
import { YBBanner, YBBannerVariant } from '../../../common/descriptors';
import { YBModal, YBModalProps, YBTooltip } from '../../../../redesign/components';

import { DrConfig } from '../types';

interface InitiateFailoverModalProps {
  drConfig: DrConfig;
  modalProps: YBModalProps;
}

const useStyles = makeStyles((theme) => ({
  instructionsHeader: {
    marginBottom: theme.spacing(3)
  },
  optionCard: {
    display: 'flex',
    flexDirection: 'column',

    height: '188px',
    padding: `${theme.spacing(2)}px ${theme.spacing(3)}px`,

    background: theme.palette.ybacolors.backgroundGrayLightest,
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: '8px',

    '&:hover': {
      cursor: 'pointer'
    }
  },
  selectedOptionCard: {
    background: theme.palette.ybacolors.backgroundBlueLight,
    border: `1px solid ${theme.palette.ybacolors.borderBlue}`
  },
  optionCardHeader: {
    display: 'flex',
    alignItems: 'center',

    marginBottom: theme.spacing(3)
  },
  dataLossSection: {
    padding: `${theme.spacing(1.5)}px 0`,
    marginTop: 'auto',

    borderTop: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`
  },
  propertyLabel: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center',

    color: theme.palette.ybacolors.textDarkGray
  },
  infoBanner: {
    marginTop: 'auto'
  },
  infoIcon: {
    '&:hover': {
      cursor: 'pointer'
    }
  },
  rpoBenchmarkIcon: {
    fontSize: '16px'
  },
  success: {
    color: theme.palette.ybacolors.success
  },
  dialogContentRoot: {
    display: 'flex',
    flexDirection: 'column'
  }
}));

const TRANSLATION_KEY_PREFIX = 'clusterDetail.disasterRecovery.failover.initiateModal';

export const InitiateFailoverModal = ({ drConfig, modalProps }: InitiateFailoverModalProps) => {
  const [failoverType, setFailoverType] = useState<FailoverType>();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  const onSubmit = () => {};

  return (
    <YBModal
      title={t('title')}
      submitLabel={t('submitButton')}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      onSubmit={onSubmit}
      maxWidth="xl"
      overrideWidth="960px"
      overrideHeight="640px"
      dialogContentProps={{
        className: classes.dialogContentRoot
      }}
      {...modalProps}
    >
      <Typography className={classes.instructionsHeader} variant="h6">
        {t('instruction')}
      </Typography>
      <Box display="flex" gridGap={theme.spacing(1)}>
        <div
          className={clsx(
            classes.optionCard,
            failoverType === FailoverType.UNPLANNED && classes.selectedOptionCard
          )}
          onClick={() => setFailoverType(FailoverType.UNPLANNED)}
        >
          <div className={classes.optionCardHeader}>
            <Typography variant="body1">{t('option.failoverImmediately.optionName')}</Typography>
            <Box display="flex" alignItems="center" marginLeft="auto">
              {failoverType === FailoverType.UNPLANNED ? <SelectedIcon /> : <UnselectedIcon />}
            </Box>
          </div>
          <Typography variant="body2">{t('option.failoverImmediately.description')}</Typography>
          <div className={classes.dataLossSection}>
            <div className={classes.propertyLabel}>
              <Typography variant="body1">{t('property.estimatedDataLoss.label')}</Typography>
              <YBTooltip title={t('property.estimatedDataLoss.tooltip')}>
                <InfoIcon className={classes.infoIcon} />
              </YBTooltip>
            </div>
            <Box display="flex" justifyContent="space-between">
              <Typography variant="body1">~600ms</Typography>
              <Box display="flex" alignItems="center">
                {/* TODO: Check estimate data loss vs. RPO to determine the icon to show. */}
                <ArrowDropDown className={clsx(classes.rpoBenchmarkIcon, classes.success)} />
                <Typography variant="body2">
                  {t('property.estimatedDataLoss.rpoBenchmark.below', { ms: 100 })}
                </Typography>
              </Box>
            </Box>
          </div>
        </div>
        <div
          className={clsx(
            classes.optionCard,
            failoverType === FailoverType.PLANNED && classes.selectedOptionCard
          )}
          onClick={() => setFailoverType(FailoverType.PLANNED)}
        >
          <div className={classes.optionCardHeader}>
            <Typography variant="body1">
              {t('option.waitForReplicationDrain.optionName')}
            </Typography>
            <Box display="flex" alignItems="center" marginLeft="auto">
              {failoverType === FailoverType.PLANNED ? <SelectedIcon /> : <UnselectedIcon />}
            </Box>
          </div>
          <Typography variant="body2">{t('option.waitForReplicationDrain.description')}</Typography>
          <div className={classes.dataLossSection}>
            <div className={classes.propertyLabel}>
              <Typography variant="body1">{t('property.estimatedDataLoss.label')}</Typography>
              <YBTooltip title={t('property.estimatedDataLoss.tooltip')}>
                <InfoIcon className={classes.infoIcon} />
              </YBTooltip>
            </div>
            <Typography variant="body1">{t('property.estimatedDataLoss.none')}</Typography>
          </div>
        </div>
      </Box>
      <YBBanner className={classes.infoBanner} variant={YBBannerVariant.INFO}>
        <Trans
          i18nKey={`${TRANSLATION_KEY_PREFIX}.note.stopWorkload`}
          components={{ bold: <b /> }}
        />
      </YBBanner>
    </YBModal>
  );
};
