import { useState, FC } from 'react';
import { useTranslation } from 'react-i18next';
import { isEmpty } from 'lodash';
import { YBButton, mui } from '@yugabyte-ui-library/core';
import { useFormContext, useWatch } from 'react-hook-form';
import { K8sHelmOverridesModal } from './K8sHelmOverridesModal';
import { OtherAdvancedProps } from '../../steps/advanced-settings/dtos';
import { AZ_OVERRIDES_FIELD, UNIVERSE_OVERRIDES_FIELD } from '../FieldNames';
import { ClusterPlacementSpec } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

//icons
import CircleAddIcon from '@app/redesign/assets/circle-add-v2.svg';
import EditIcon from '@app/redesign/assets/edit-primary.svg';

const { Box, Typography } = mui;

interface K8sHelmOverridesCardProps {
  placementSpec: ClusterPlacementSpec;
  dbVersion: string;
}

export const K8sHelmOverridesCard: FC<K8sHelmOverridesCardProps> = ({
  placementSpec,
  dbVersion
}) => {
  const [showOverridesModal, setShowOverridesModal] = useState(false);
  const { setValue } = useFormContext<OtherAdvancedProps>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings'
  });
  //watchers
  const azOverrides = useWatch({ name: AZ_OVERRIDES_FIELD });
  const universeOverrides = useWatch({ name: UNIVERSE_OVERRIDES_FIELD });

  //handlers
  const handleClose = () => setShowOverridesModal(false);

  const handleSubmit = (universeOverrides: string, azOverrides: Record<string, string>) => {
    setValue(UNIVERSE_OVERRIDES_FIELD, universeOverrides);
    setValue(AZ_OVERRIDES_FIELD, azOverrides);
  };

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px', padding: '0px 8px 12px' }}>
      <Typography variant="body2">{t('overrideInfo')}</Typography>
      {isEmpty(azOverrides) && isEmpty(universeOverrides) ? (
        <YBButton
          variant="secondary"
          data-testid={`UniverseNameField-AddHelmButton`}
          onClick={() => setShowOverridesModal(true)}
          size="medium"
          startIcon={<CircleAddIcon />}
          sx={{ width: 'fit-content' }}
          dataTestId="add-helm-overrides-button"
        >
          {t('addHelmOverrides')}
        </YBButton>
      ) : (
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'row',
            justifyContent: 'space-between',
            padding: '8px 16px',
            borderRadius: '8px',
            border: '1px solid #D7DEE4'
          }}
        >
          <Box
            sx={{
              display: 'flex',
              backgroundColor: '#E5EDFF',
              borderRadius: '6px',
              color: '#2B59C3',
              padding: '4px 6px',
              width: 'fit-content',
              lineHeight: '16px',
              alignItems: 'center'
            }}
          >
            <Typography variant="subtitle1">{t('overrideConfigured')}</Typography>
          </Box>
          <YBButton
            variant="secondary"
            data-testid={`UniverseNameField-EditHelmButton`}
            onClick={() => setShowOverridesModal(true)}
            size="medium"
            startIcon={<EditIcon />}
            sx={{ width: 'fit-content' }}
            dataTestId="add-helm-overrides-button"
          >
            {t('editHelmOverrides')}
          </YBButton>
        </Box>
      )}
      {showOverridesModal && (
        <K8sHelmOverridesModal
          initialValues={{ azOverrides, universeOverrides }}
          open={showOverridesModal}
          onClose={handleClose}
          onSubmit={handleSubmit}
          placementSpec={placementSpec}
          dbVersion={dbVersion}
        />
      )}
    </Box>
  );
};
