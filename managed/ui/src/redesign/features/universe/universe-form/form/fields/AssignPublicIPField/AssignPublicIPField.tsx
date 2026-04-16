import { ReactElement, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBLabel, YBToggleField, YBTooltip } from '../../../../../../components';
import { CloudType, UniverseFormData } from '../../../utils/dto';
import { ASSIGN_PUBLIC_IP_FIELD } from '../../../utils/constants';
import InfoMessageIcon from '../../../../../../assets/info-message.svg';

interface AssignPublicIPFieldProps {
  disabled: boolean;
  isCreateMode: boolean;
  providerCode: CloudType;
}

export const AssignPublicIPField = ({
  disabled,
  isCreateMode,
  providerCode
}: AssignPublicIPFieldProps): ReactElement => {
  const { control, setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();
  const assignIPTooltipText = t('universeForm.instanceConfig.assignPublicIPHelper');

  useEffect(() => {
    if (isCreateMode) {
      providerCode === CloudType.azu
        ? setValue(ASSIGN_PUBLIC_IP_FIELD, false)
        : setValue(ASSIGN_PUBLIC_IP_FIELD, true);
    }
  }, [providerCode]);

  return (
    <Box
      display="flex"
      width="100%"
      data-testid="AssignPublicIPField-Container"
      mt={2}
      mb={2}
      ml={2}
    >
      <YBToggleField
        name={ASSIGN_PUBLIC_IP_FIELD}
        inputProps={{
          'data-testid': 'AssignPublicIPField-Toggle'
        }}
        control={control}
        disabled={disabled}
      />

      <Box flex={1} alignSelf="center">
        <YBLabel dataTestId="AssignPublicIPField-Label">
          {t('universeForm.instanceConfig.assignPublicIP')}
          &nbsp;
          <YBTooltip title={assignIPTooltipText}>
            <img alt="Info" src={InfoMessageIcon} />
          </YBTooltip>
        </YBLabel>
      </Box>
    </Box>
  );
};

//shown only for aws, gcp, azu
//disabled for non primary cluster
