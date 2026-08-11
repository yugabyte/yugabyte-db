import { FC } from 'react';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { YBToggleField } from '@yugabyte-ui-library/core';
import { InstanceSettingProps } from '@app/redesign/features-v2/universe/create-universe/steps/hardware-settings/dtos';
import { ENABLE_EBS_CONFIG_FIELD } from '@app/redesign/features-v2/universe/create-universe/fields/FieldNames';

interface EBSVolumeFieldProps {
  disabled: boolean;
}

export const EBSVolumeField: FC<EBSVolumeFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<InstanceSettingProps>();
  const { t } = useTranslation('translation');

  return (
    <YBToggleField
      disabled={disabled}
      name={ENABLE_EBS_CONFIG_FIELD}
      control={control}
      dataTestId="ebs-volume-toggle-field"
      label={t('createUniverseV2.instanceSettings.EBSVolume.title')}
    />
  );
};
