import { ReactElement } from 'react';
import { Box } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { UniverseFormData } from '../../../utils/dto';
import { YBLabel, YBToggleField } from '../../../../../../components';
import { AUTO_PLACEMENT_FIELD } from '../../../utils/constants';
interface AutoPlacementFieldProps {
  disabled: boolean;
}

export const AutoPlacementField = ({ disabled }: AutoPlacementFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%">
      <YBLabel>{t('universeForm.cloudConfig.autoPlacementField')}</YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={AUTO_PLACEMENT_FIELD}
          inputProps={{
            'data-testid': 'ToggleAutoPlacement'
          }}
          control={control}
          disabled={disabled}
        />
        {/* <YBHelper>{t('universeForm.instanceConfig.assignPublicIPHelper')}</YBHelper> */}
      </Box>
    </Box>
  );
};
