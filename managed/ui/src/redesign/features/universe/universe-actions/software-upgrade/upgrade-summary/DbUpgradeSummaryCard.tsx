import { makeStyles, Typography } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';

import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { DbUpgradeFormStep, UpgradeMethod } from '../constants';
import { DBUpgradeFormFields } from '../types';
import { hasPassedOrReachedFormStep } from '../utils/formUtils';
import { SummaryRow } from './components/SummaryRow';
import { UpgradePaceAndBatchSummary } from './components/UpgradePaceAndBatchSummary';
import { UpgradePlanList } from './components/UpgradePlanList';

const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal.summaryCard';

interface DbUpgradeSummaryCardProps {
  currentFormStep: DbUpgradeFormStep;
  formSteps: DbUpgradeFormStep[];
}

const useStyles = makeStyles((theme) => ({
  infoCard: {
    background: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  infoCardHeader: {
    ...theme.typography.body1,
    padding: `${theme.spacing(2.5)}px ${theme.spacing(2)}px`,
    borderBottom: `1px solid ${theme.palette.grey[200]}`
  },
  summaryBody: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(4),
    padding: `${theme.spacing(3.5)}px ${theme.spacing(2)}px`
  }
}));

export const DbUpgradeSummaryCard = ({ currentFormStep, formSteps }: DbUpgradeSummaryCardProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();

  const { watch } = useFormContext<Partial<DBUpgradeFormFields>>();
  const targetDbVersion = watch('targetDbVersion');
  const upgradeMethod = watch('upgradeMethod');
  const upgradePace = watch('upgradePace');
  const maxNodesPerBatch = watch('maxNodesPerBatch');
  const waitBetweenBatchesSeconds = watch('waitBetweenBatchesSeconds');
  const canaryUpgradeConfig = watch('canaryUpgradeConfig');

  return (
    <div className={classes.infoCard}>
      <Typography className={classes.infoCardHeader} variant="body1">
        Summary
      </Typography>
      <div className={classes.summaryBody}>
        <SummaryRow label={t('targetVersion')}>
          {targetDbVersion ? <b>{formatYbSoftwareVersionString(targetDbVersion)}</b> : '-'}
        </SummaryRow>
        {hasPassedOrReachedFormStep(
          formSteps,
          currentFormStep,
          DbUpgradeFormStep.UPGRADE_METHOD
        ) && (
          <SummaryRow label={t('upgradeMethod')}>
            {upgradeMethod === UpgradeMethod.EXPRESS ? t('expressUpgrade') : t('canaryUpgrade')}
          </SummaryRow>
        )}
        {hasPassedOrReachedFormStep(formSteps, currentFormStep, DbUpgradeFormStep.UPGRADE_METHOD) &&
          upgradeMethod === UpgradeMethod.EXPRESS && (
            <UpgradePaceAndBatchSummary
              upgradePace={upgradePace}
              maxNodesPerBatch={maxNodesPerBatch}
              waitBetweenBatchesSeconds={waitBetweenBatchesSeconds}
              translationKeyPrefix={TRANSLATION_KEY_PREFIX}
            />
          )}
        {hasPassedOrReachedFormStep(formSteps, currentFormStep, DbUpgradeFormStep.UPGRADE_PLAN) && (
          <SummaryRow label={t('upgradePlan')}>
            <UpgradePlanList canaryUpgradeConfig={canaryUpgradeConfig} />
          </SummaryRow>
        )}
        {hasPassedOrReachedFormStep(formSteps, currentFormStep, DbUpgradeFormStep.UPGRADE_PACE) &&
          upgradeMethod === UpgradeMethod.CANARY && (
            <UpgradePaceAndBatchSummary
              upgradePace={upgradePace}
              maxNodesPerBatch={maxNodesPerBatch}
              waitBetweenBatchesSeconds={waitBetweenBatchesSeconds}
              translationKeyPrefix={TRANSLATION_KEY_PREFIX}
            />
          )}
      </div>
    </div>
  );
};
