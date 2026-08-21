import { ReactNode } from 'react';
import { FormHelperText, makeStyles, Typography } from '@material-ui/core';
import { get } from 'lodash';
import {
  useWatch,
  type Control,
  type FieldError,
  type FieldErrors,
  type FieldPathByValue,
  type FieldValues
} from 'react-hook-form';
import { useTranslation } from 'react-i18next';

import { YBInputField, YBTooltip, type YBInputProps } from '@app/redesign/components';

import InfoIcon from '@app/redesign/assets/info-message.svg';

const useStyles = makeStyles((theme) => ({
  rollingUpdateSettings: {
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
  updatePaceFormFieldContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(0.5)
  },
  updatePaceFormField: {
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
  },
  tooltipIconWrapper: {
    lineHeight: 0
  }
}));

type NumericFieldPath<TFieldValues extends FieldValues> = FieldPathByValue<
  TFieldValues,
  number | undefined
>;

const ROLLING_UPDATE_BATCH_SETTINGS_I18N_PREFIX =
  'universeActions.dbUpgrade.sharedComponents.rollingUpdateBatchSettings';

interface RollingUpdateBatchSettingsProps<TFieldValues extends FieldValues> {
  control: Control<TFieldValues>;
  errors: FieldErrors<TFieldValues>;
  maxNodesPerBatchName: NumericFieldPath<TFieldValues>;
  waitBetweenBatchesName: NumericFieldPath<TFieldValues>;
  maxNodesPerBatchMaximum: number;
  shouldValidate: (formValues: TFieldValues) => boolean;
  isRollbackFlow?: boolean;
  maxNodesPerBatchTooltip?: ReactNode;
  waitBetweenBatchesTooltip?: ReactNode;
  testIdPrefix: string;
  isDisabled: boolean;
  maxNodesPerBatchOnBlur?: YBInputProps['onBlur'];
}

export const RollingUpdateBatchSettings = <TFieldValues extends FieldValues>({
  control,
  errors,
  maxNodesPerBatchName,
  waitBetweenBatchesName,
  maxNodesPerBatchMaximum,
  shouldValidate,
  isRollbackFlow = false,
  maxNodesPerBatchTooltip,
  waitBetweenBatchesTooltip,
  testIdPrefix,
  isDisabled,
  maxNodesPerBatchOnBlur
}: RollingUpdateBatchSettingsProps<TFieldValues>) => {
  const { t } = useTranslation('translation');
  const classes = useStyles();
  const watchedFormValues = useWatch({ control }) as TFieldValues;
  const showRollingBatchFieldErrors = shouldValidate(watchedFormValues);
  const maxNodesPerBatchInputDisabled = isDisabled || maxNodesPerBatchMaximum <= 1;
  const waitBetweenBatchesInputDisabled = isDisabled;
  const maxNodesPerBatchError = get(errors, maxNodesPerBatchName) as FieldError | undefined;
  const waitBetweenBatchesError = get(errors, waitBetweenBatchesName) as FieldError | undefined;
  const maxNodesPerBatchLabel = t('maxNodesPerBatch', {
    keyPrefix: ROLLING_UPDATE_BATCH_SETTINGS_I18N_PREFIX
  });
  const maxNodesPerBatchTooltipText =
    maxNodesPerBatchTooltip ??
    t(isRollbackFlow ? 'maxNodesPerBatchTooltipRollback' : 'maxNodesPerBatchTooltip', {
      keyPrefix: ROLLING_UPDATE_BATCH_SETTINGS_I18N_PREFIX
    });
  const waitBetweenBatchesLabel = t('waitBetweenBatches', {
    keyPrefix: ROLLING_UPDATE_BATCH_SETTINGS_I18N_PREFIX
  });
  const waitBetweenBatchesTooltipText =
    waitBetweenBatchesTooltip ??
    t('waitBetweenBatchesTooltip', { keyPrefix: ROLLING_UPDATE_BATCH_SETTINGS_I18N_PREFIX });
  const secondsLabel = t('seconds', { keyPrefix: ROLLING_UPDATE_BATCH_SETTINGS_I18N_PREFIX });
  const requiredValidationMessage = t('formFieldRequired', { keyPrefix: 'common' });
  const maxNodesPerBatchMinimumValidationMessage = t(
    'validationError.maxNodesPerBatchMinimum',
    {
      keyPrefix: ROLLING_UPDATE_BATCH_SETTINGS_I18N_PREFIX
    }
  );
  const maxNodesPerBatchMaximumValidationMessage = t(
    'validationError.maxNodesPerBatchMaximum',
    {
      keyPrefix: ROLLING_UPDATE_BATCH_SETTINGS_I18N_PREFIX,
      max: maxNodesPerBatchMaximum
    }
  );
  const waitBetweenBatchesMinimumValidationMessage = t(
    'validationError.waitBetweenBatchesMinimum',
    { keyPrefix: ROLLING_UPDATE_BATCH_SETTINGS_I18N_PREFIX }
  );

  return (
    <div className={classes.rollingUpdateSettings}>
      <div className={classes.updatePaceFormFieldContainer}>
        <div className={classes.updatePaceFormField}>
          <Typography className={classes.settingLabel}>{maxNodesPerBatchLabel}</Typography>
          <YBInputField<TFieldValues>
            control={control}
            name={maxNodesPerBatchName}
            type="number"
            className={classes.numericInputField}
            disabled={maxNodesPerBatchInputDisabled}
            onBlur={maxNodesPerBatchOnBlur}
            hideInlineError
            rules={{
              validate: (value: unknown, formValues: TFieldValues) => {
                if (!shouldValidate(formValues)) {
                  return true;
                }
                if (value === undefined || value === null || value === '') {
                  return requiredValidationMessage;
                }
                const numericValue = Number(value);
                if (numericValue < 1) {
                  return maxNodesPerBatchMinimumValidationMessage;
                }
                if (numericValue > maxNodesPerBatchMaximum) {
                  return maxNodesPerBatchMaximumValidationMessage;
                }
                return true;
              }
            }}
            inputProps={{
              min: 1,
              max: maxNodesPerBatchMaximum,
              'data-testid': `${testIdPrefix}-MaxBatchInput`
            }}
          />
          <YBTooltip
            title={
              <Typography variant="body2" component="span" style={{ whiteSpace: 'pre-line' }}>
                {maxNodesPerBatchTooltipText}
              </Typography>
            }
          >
            <span className={classes.tooltipIconWrapper}>
              <InfoIcon width={18} height={18} />
            </span>
          </YBTooltip>
        </div>
        {showRollingBatchFieldErrors && maxNodesPerBatchError && (
          <FormHelperText error={true}>{maxNodesPerBatchError.message}</FormHelperText>
        )}
      </div>
      <div className={classes.updatePaceFormFieldContainer}>
        <div className={classes.updatePaceFormField}>
          <Typography className={classes.settingLabel}>{waitBetweenBatchesLabel}</Typography>
          <YBInputField<TFieldValues>
            control={control}
            name={waitBetweenBatchesName}
            type="number"
            className={classes.numericInputField}
            disabled={waitBetweenBatchesInputDisabled}
            hideInlineError
            rules={{
              validate: (value: unknown, formValues: TFieldValues) => {
                if (!shouldValidate(formValues)) {
                  return true;
                }
                if (value === undefined || value === null || value === '') {
                  return requiredValidationMessage;
                }
                const numericValue = Number(value);
                if (numericValue < 0) {
                  return waitBetweenBatchesMinimumValidationMessage;
                }
                return true;
              }
            }}
            inputProps={{
              min: 0,
              'data-testid': `${testIdPrefix}-WaitInput`
            }}
          />
          <Typography className={classes.settingLabel}>{secondsLabel}</Typography>
          <YBTooltip
            title={
              <Typography variant="body2" component="span" style={{ whiteSpace: 'pre-line' }}>
                {waitBetweenBatchesTooltipText}
              </Typography>
            }
          >
            <span className={classes.tooltipIconWrapper}>
              <InfoIcon width={18} height={18} />
            </span>
          </YBTooltip>
        </div>
        {showRollingBatchFieldErrors && waitBetweenBatchesError && (
          <FormHelperText error={true}>{waitBetweenBatchesError.message}</FormHelperText>
        )}
      </div>
    </div>
  );
};
