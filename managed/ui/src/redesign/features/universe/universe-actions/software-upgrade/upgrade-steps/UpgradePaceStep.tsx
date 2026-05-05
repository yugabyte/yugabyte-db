import { FocusEvent } from 'react';
import { FormHelperText, makeStyles, Typography } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';

import { YBInputField } from '@app/redesign/components';
import { useDbUpgradeModalContext } from '@app/redesign/features/universe/universe-actions/software-upgrade/DbUpgradeModalContext';
import type { DBUpgradeFormFields } from '@app/redesign/features/universe/universe-actions/software-upgrade/types';

const TRANSLATION_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal.upgradePaceStep';
const UPGRADE_MODAL_KEY_PREFIX = 'universeActions.dbUpgrade.upgradeModal';
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
  },
  rollingSettings: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    width: 550,
    maxWidth: '100%',
    padding: theme.spacing(1.5, 2),

    backgroundColor: theme.palette.ybacolors.grey005,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  upgradePaceFormFieldContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.5)
  },
  upgradePaceFormField: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1)
  },
  settingLabel: {
    flexShrink: 0,

    color: theme.palette.grey[900],
    fontSize: 13,
    fontWeight: 400,
    lineHeight: '16px'
  },
  numericInputField: {
    flexShrink: 0,

    width: 100,

    '& .MuiInputBase-root': {
      height: 32
    }
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
      <div className={classes.rollingSettings}>
        <div className={classes.upgradePaceFormFieldContainer}>
          <div className={classes.upgradePaceFormField}>
            <Typography className={classes.settingLabel}>
              {t('fields.maxNodesPerBatch', {
                keyPrefix: UPGRADE_MODAL_KEY_PREFIX
              })}
            </Typography>
            <YBInputField
              control={control}
              name="maxNodesPerBatch"
              type="number"
              className={classes.numericInputField}
              disabled={isFormDisabled || maxNodesPerBatchMaximum <= 1}
              onBlur={handleMaxNodesPerBatchBlur}
              rules={{
                required: {
                  value: true,
                  message: t('formFieldRequired', { keyPrefix: 'common' })
                },
                min: {
                  value: 1,
                  message: t('fields.validationError.maxNodesPerBatchMinimum', {
                    keyPrefix: UPGRADE_MODAL_KEY_PREFIX
                  })
                },
                max: {
                  value: maxNodesPerBatchMaximum,
                  message: t(
                    'fields.validationError.maxNodesPerBatchMaximum',
                    {
                      keyPrefix: UPGRADE_MODAL_KEY_PREFIX,
                      max: maxNodesPerBatchMaximum
                    }
                  )
                }
              }}
              inputProps={{
                min: 1,
                max: maxNodesPerBatchMaximum,
                'data-testid': `${TEST_ID_PREFIX}-MaxBatchInput`
              }}
            />
          </div>
          {formState.errors.maxNodesPerBatch && (
            <FormHelperText error={true}>
              {formState.errors.maxNodesPerBatch.message}
            </FormHelperText>
          )}
        </div>
        <div className={classes.upgradePaceFormFieldContainer}>
          <div className={classes.upgradePaceFormField}>
            <Typography className={classes.settingLabel}>
              {t('fields.waitBetweenBatches', {
                keyPrefix: UPGRADE_MODAL_KEY_PREFIX
              })}
            </Typography>
            <YBInputField
              control={control}
              name="waitBetweenBatchesSeconds"
              type="number"
              className={classes.numericInputField}
              disabled={isFormDisabled}
              hideInlineError
              rules={{
                required: {
                  value: true,
                  message: t('formFieldRequired', { keyPrefix: 'common' })
                },
                min: {
                  value: 0,
                  message: t(
                    'fields.validationError.waitBetweenBatchesMinimum',
                    { keyPrefix: UPGRADE_MODAL_KEY_PREFIX }
                  )
                }
              }}
              inputProps={{
                min: 0,
                'data-testid': `${TEST_ID_PREFIX}-WaitInput`
              }}
            />
            <Typography className={classes.settingLabel}>
              {t('fields.seconds', {
                keyPrefix: UPGRADE_MODAL_KEY_PREFIX
              })}
            </Typography>
          </div>
          {formState.errors.waitBetweenBatchesSeconds && (
            <FormHelperText error={true}>
              {formState.errors.waitBetweenBatchesSeconds.message}
            </FormHelperText>
          )}
        </div>
      </div>
    </div>
  );
};
