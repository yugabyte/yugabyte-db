import { ReactElement, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useFormContext, useWatch } from 'react-hook-form';
import { Box } from '@material-ui/core';
import { YBButton } from '../../../../../../components';
import { UniverseFormData, Placement, AZOverridePerAZ } from '../../../utils/dto';
import { K8S_AZ_OVERRIDES_FIELD } from '../../../utils/constants';
import { AZOverridesModal } from './AZOverridesModal';

interface AZOverridesFieldProps {
  disabled: boolean;
  placements: Placement[];
}

export const AZOverridesField = ({
  disabled,
  placements
}: AZOverridesFieldProps): ReactElement => {
  const [showModal, setShowModal] = useState(false);
  const { setValue } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  const k8sAzOverrides = useWatch({ name: K8S_AZ_OVERRIDES_FIELD });

  const hasExistingOverrides = Object.keys(k8sAzOverrides ?? {}).length > 0;

  const handleClose = () => setShowModal(false);

  const handleSubmit = (newAzOverrides: Record<string, AZOverridePerAZ>) => {
    setValue(K8S_AZ_OVERRIDES_FIELD, newAzOverrides, { shouldDirty: true });
  };

  return (
    <>
      {showModal && (
        <AZOverridesModal
          open={showModal}
          onClose={handleClose}
          onSubmit={handleSubmit}
          placements={placements}
          initialAzOverrides={k8sAzOverrides ?? {}}
        />
      )}
      <Box mt={2}>
        <YBButton
          disabled={disabled || placements.length === 0}
          variant="primary"
          data-testid="AZOverridesField-Button"
          onClick={() => setShowModal(true)}
        >
          <span className={hasExistingOverrides ? 'fa fa-pencil' : 'fa fa-plus'} />
          {hasExistingOverrides
            ? t('universeForm.azOverrides.editStorageOverrides')
            : t('universeForm.azOverrides.addStorageOverrides')}
        </YBButton>
      </Box>
    </>
  );
};
