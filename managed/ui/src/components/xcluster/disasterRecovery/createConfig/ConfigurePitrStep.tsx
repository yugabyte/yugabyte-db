import { useEffect } from 'react';
import { Box, Typography, useTheme } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';

import InfoIcon from '../../../../redesign/assets/info-message.svg';
import {
  ReactSelectOption,
  YBReactSelectField
} from '../../../configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';
import { CreateDrConfigFormValues } from './CreateConfigModal';
import { DurationUnit } from '../constants';
import { I18N_DURATION_KEY_PREFIX } from '../../../../redesign/helpers/constants';
import { YBInputField, YBTooltip } from '../../../../redesign/components';
import { getPitrRetentionPeriodMinValue } from '../utils';

import { useModalStyles } from '../styles';

interface ConfigureAlertStepProps {
  isFormDisabled: boolean;
}

const TRANSLATION_KEY_PREFIX =
  'clusterDetail.disasterRecovery.config.createModal.step.configurePitr';

export const ConfigurePitrStep = ({ isFormDisabled }: ConfigureAlertStepProps) => {
  const { control, watch, setValue, trigger, formState } = useFormContext<
    CreateDrConfigFormValues
  >();
  const theme = useTheme();
  const modalClasses = useModalStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });

  const UNIT_OPTIONS: ReactSelectOption[] = [
    {
      label: t('seconds', { keyPrefix: I18N_DURATION_KEY_PREFIX }),
      value: DurationUnit.SECOND
    },
    {
      label: t('minutes', { keyPrefix: I18N_DURATION_KEY_PREFIX }),
      value: DurationUnit.MINUTE
    },
    {
      label: t('hours', { keyPrefix: I18N_DURATION_KEY_PREFIX }),
      value: DurationUnit.HOUR
    },
    { label: t('days', { keyPrefix: I18N_DURATION_KEY_PREFIX }), value: DurationUnit.DAY }
  ];

  const pitrRetentionPeriodValue = watch('pitrRetentionPeriodValue');
  const pitrRetentionPeriodUnit = watch('pitrRetentionPeriodUnit')?.value;

  useEffect(() => {
    // Changing the retention period unit might clear an error on
    // `pitrRetentionPeriodValue`. Ex. `pitrRetentionPeriodValue` = 6 does not pass validation when
    // the unit is seconds, but it does pass validation when the unit is minutes.
    if (pitrRetentionPeriodUnit !== undefined) {
      trigger('pitrRetentionPeriodValue');
    }
  }, [pitrRetentionPeriodUnit]);

  // We enforce a minimum snapshot interval of 5 minutes to prevent extremely short intervals.
  // More frequent snapshots cause more disk usage because the compactions won't be as optimal due to
  // more smaller sst tables being flushed every time a snapshot is created.
  const pitrRetentionPeriodMinValue = getPitrRetentionPeriodMinValue(pitrRetentionPeriodUnit);

  const handlePitrRetentionPeriodUnitChange = (option: ReactSelectOption) => {
    const pitrRetentionPeriodMinValue = getPitrRetentionPeriodMinValue(
      option.value as DurationUnit
    );
    // `pitrRetentionPeriodValue` is undefined when the user hasn't entered a value yet.
    if (
      pitrRetentionPeriodValue === undefined ||
      pitrRetentionPeriodMinValue > pitrRetentionPeriodValue
    ) {
      setValue('pitrRetentionPeriodValue', pitrRetentionPeriodMinValue, { shouldValidate: true });
    }
  };

  return (
    <div className={modalClasses.stepContainer}>
      <ol start={3}>
        <li>
          <Typography variant="body1" className={modalClasses.instruction}>
            {t('instruction')}
          </Typography>
          <div className={modalClasses.fieldLabel}>
            <Typography variant="body2">{t('retentionPeriodSeconds.label')}</Typography>
            <YBTooltip
              title={<Typography variant="body2">{t('retentionPeriodSeconds.tooltip')}</Typography>}
            >
              <img src={InfoIcon} alt={t('infoIcon', { keyPrefix: 'imgAltText' })} />
            </YBTooltip>
          </div>
          <Box display="flex" gridGap={theme.spacing(1)} alignItems="flex-start">
            <YBInputField
              control={control}
              name="pitrRetentionPeriodValue"
              type="number"
              inputProps={{ min: pitrRetentionPeriodMinValue }}
              rules={{
                required: t('error.pitrRetentionPeriodValueRequired'),
                min: {
                  value: pitrRetentionPeriodMinValue,
                  message: t('error.pitrRetentionPeriodValueMinimum')
                }
              }}
              disabled={isFormDisabled}
            />
            <YBReactSelectField
              control={control}
              name="pitrRetentionPeriodUnit"
              onChange={handlePitrRetentionPeriodUnitChange}
              options={UNIT_OPTIONS}
              autoSizeMinWidth={200}
              maxWidth="100%"
              rules={{ required: t('error.pitrRetentionPeriodUnitRequired') }}
              isDisabled={isFormDisabled}
            />
          </Box>
        </li>
      </ol>
    </div>
  );
};
