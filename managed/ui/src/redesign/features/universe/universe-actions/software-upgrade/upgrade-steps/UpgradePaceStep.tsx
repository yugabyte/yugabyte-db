import { FocusEvent } from 'react';
import { makeStyles, Typography } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';

import { useDbUpgradeModalContext } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbUpgradeModalContext';
import type { DBUpgradeFormFields } from '@app/redesign/features/universe/universe-actions/software-upgrade/types';
import { RollingUpdateBatchSettings } from '../components/RollingUpdateBatchSettings';

const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal.upgradePaceStep';
const TEST_ID_PREFIX = 'UpgradePaceStep';

const useStyles = makeStyles((theme) => ({
  stepContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(3),

    width: '100%',

    padding: theme.spacing(3)
  },
  stepHeader: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  }
}));

export const UpgradePaceStep = () => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const { control, setValue, formState } = useFormContext<DBUpgradeFormFields>();
  const { maxNodesPerBatchMaximum } = useDbUpgradeModalContext();

  const isFormDisabled = formState.isSubmitting;

  const handleMaxNodesPerBatchBlur = (event: FocusEvent<HTMLInputElement>) => {
    const fieldValue = Number(event.target.value);
    if (fieldValue > maxNodesPerBatchMaximum) {
      setValue('maxNodesPerBatch', maxNodesPerBatchMaximum);
    } else if (fieldValue < 1) {
      setValue('maxNodesPerBatch', 1);
    }
  };

  return (
    <div className={classes.stepContainer}>
      <div className={classes.stepHeader}>
        <Typography variant="h5">{t('stepTitle')}</Typography>
      </div>
      <RollingUpdateBatchSettings<DBUpgradeFormFields>
        control={control}
        errors={formState.errors}
        maxNodesPerBatchName="maxNodesPerBatch"
        waitBetweenBatchesName="waitBetweenBatchesSeconds"
        maxNodesPerBatchMaximum={maxNodesPerBatchMaximum}
        shouldValidate={() => true}
        isDisabled={isFormDisabled}
        maxNodesPerBatchOnBlur={handleMaxNodesPerBatchBlur}
        testIdPrefix={TEST_ID_PREFIX}
      />
    </div>
  );
};
