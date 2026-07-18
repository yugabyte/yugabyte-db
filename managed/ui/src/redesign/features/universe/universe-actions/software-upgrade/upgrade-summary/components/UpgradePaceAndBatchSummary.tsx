import { makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';

import { UpgradePace } from '../../constants';
import { SummaryRow } from './SummaryRow';

interface UpgradePaceAndBatchSummaryProps {
  upgradePace: UpgradePace | undefined;
  maxNodesPerBatch: number | undefined;
  waitBetweenBatchesSeconds: number | undefined;
  translationKeyPrefix: string;
}

const useStyles = makeStyles((theme) => ({
  batchParamsSection: {
    display: 'grid',
    gridTemplateColumns: 'max-content 1fr',
    columnGap: theme.spacing(2),
    rowGap: theme.spacing(0.75),
    alignItems: 'baseline'
  },
  batchLabel: {
    fontWeight: 500,
    fontSize: '11.5px',
    lineHeight: '16px',
    color: theme.palette.grey[600]
  },
  batchValue: {
    lineHeight: '16px',
    color: theme.palette.grey[900]
  }
}));

export const UpgradePaceAndBatchSummary = ({
  upgradePace,
  maxNodesPerBatch,
  waitBetweenBatchesSeconds,
  translationKeyPrefix
}: UpgradePaceAndBatchSummaryProps) => {
  const { t } = useTranslation('translation', { keyPrefix: translationKeyPrefix });
  const classes = useStyles();

  return (
    <>
      <SummaryRow label={t('upgradePace')}>
        {upgradePace === UpgradePace.ROLLING ? t('rollingUpgrade') : t('concurrentUpgrade')}
      </SummaryRow>
      {upgradePace === UpgradePace.ROLLING && (
        <SummaryRow label={t('batch')}>
          <div className={classes.batchParamsSection}>
            <Typography className={classes.batchLabel}>{t('maxSize')}</Typography>
            <Typography variant="body2" className={classes.batchValue}>
              {maxNodesPerBatch !== undefined
                ? maxNodesPerBatch === 1
                  ? t('nodeCount_one', { count: maxNodesPerBatch })
                  : t('nodeCount_other', { count: maxNodesPerBatch })
                : '-'}
            </Typography>
            <Typography className={classes.batchLabel}>{t('waitTime')}</Typography>
            <Typography variant="body2" className={classes.batchValue}>
              {waitBetweenBatchesSeconds !== undefined
                ? `${waitBetweenBatchesSeconds}${t('unitAbbreviation.seconds', { keyPrefix: 'common' })}`
                : '-'}
            </Typography>
          </div>
        </SummaryRow>
      )}
    </>
  );
};
