import { FC } from 'react';
import { Box } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { YBLabel, YBToggleField } from '../../../../../../components';
import { ENABLE_EBS_CONFIG_FIELD } from '../../../utils/constants';
import { UniverseFormData } from '../../../utils/dto';

interface EBSVolumeFieldProps {
  disabled: boolean;
}

export const EBSVolumeField: FC<EBSVolumeFieldProps> = ({ disabled }) => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  return (
    <Box display="flex" width="100%" mt={2}>
      <YBLabel dataTestId={`EBSVolumeField-Label`}>
        {t('universeForm.instanceConfig.EBSVolume.title')}
      </YBLabel>
      <Box flex={1}>
        <YBToggleField disabled={disabled} name={ENABLE_EBS_CONFIG_FIELD} control={control} />
      </Box>
    </Box>
  );
};
