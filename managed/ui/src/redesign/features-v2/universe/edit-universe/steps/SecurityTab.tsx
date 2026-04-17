import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { mui, YBButton } from '@yugabyte-ui-library/core';
import {
  StyledContent,
  StyledHeader,
  StyledInfoRow,
  StyledPanel
} from '../../create-universe/components/DefaultComponents';
import { EncryptionInTransit } from '@app/redesign/features/universe/universe-actions/encryption-in-transit/EncryptionInTransit';
import { EncryptionAtRest } from '@app/redesign/features/universe/universe-actions/encryption-at-rest/EncryptionAtRest';
import { api, QUERY_KEY } from '@app/redesign/utils/api';
import { FormProvider, useForm } from 'react-hook-form';
import { SecuritySettingsProps } from '../../create-universe/steps/security-settings/dtos';
import { AssignPublicIPField } from '../../create-universe/fields';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType } from '@app/redesign/helpers/dtos';

import Checked from '@app/redesign/assets/check-new.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import Disabled from '@app/redesign/assets/revoke.svg';

const { styled, Box, CircularProgress } = mui;

const ContentArea = styled(StyledContent)({
  '& .yb-MuiFormControlLabel-label': {
    marginLeft: '5px !important'
  }
});

const CheckedIcon = styled(Checked)({
  width: '24px',
  height: '24px',
  marginTop: '0 !important'
});

const DisabledIcon = styled(Disabled)({
  width: '24px',
  height: '24px',
  marginTop: '0 !important'
});

export const SecurityTab = () => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.security' });
  const queryClient = useQueryClient();
  const methods = useForm<SecuritySettingsProps>();
  const { universeData } = useEditUniverseContext();
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);

  const [eitModalOpen, setEitModalOpen] = useState(false);
  const [earModalOpen, setEarModalOpen] = useState(false);
  const universeUUID = universeData?.info?.universe_uuid;

  const { data: legacyUniverse, isLoading: isLegacyUniverseLoading } = useQuery(
    [QUERY_KEY.fetchUniverse, universeUUID],
    () => api.fetchUniverse(universeUUID!),
    { enabled: !!universeUUID }
  );

  const earConfig = legacyUniverse?.universeDetails?.encryptionAtRestConfig;
  const encryptionAtRestEnabled = !!(
    earConfig?.encryptionAtRestEnabled ?? earConfig?.kmsConfigUUID
  );

  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const nodeToNodeEnabled =
    !!universeData?.spec?.encryption_in_transit_spec?.enable_node_to_node_encrypt;
  const clientToNodeEnabled =
    !!universeData?.spec?.encryption_in_transit_spec?.enable_client_to_node_encrypt;

  const isItKubernetesUniverse = providerCode === CloudType.kubernetes;

  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
        <StyledPanel>
          <StyledHeader>{t('publicIPAssignment')}</StyledHeader>
          <ContentArea>
            <AssignPublicIPField providerCode={providerCode!} disabled={false} />
          </ContentArea>
        </StyledPanel>
        <StyledPanel>
          <StyledHeader
            sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
          >
            {t('encryptionInTransit')}
            <YBButton
              dataTestId="edit-security-transit-button"
              variant="ghost"
              startIcon={<EditIcon />}
              onClick={() => setEitModalOpen(true)}
              disabled={eitModalOpen}
            >
              {t('edit', { keyPrefix: 'common' })}
            </YBButton>
          </StyledHeader>
          <StyledContent>
            <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
              <div>
                <span className="header">{t('nodeToNode')}</span>
                <span className="value sameline nogap">
                  {t(nodeToNodeEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                  {nodeToNodeEnabled ? <CheckedIcon /> : <DisabledIcon />}
                </span>
              </div>
              <div>
                <span className="header">{t('clientToNode')}</span>
                <span className="value sameline nogap">
                  {t(clientToNodeEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                  {clientToNodeEnabled ? <CheckedIcon /> : <DisabledIcon />}
                </span>
              </div>
            </StyledInfoRow>
          </StyledContent>
        </StyledPanel>
        <StyledPanel>
          <StyledHeader
            sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
          >
            {t('encryptionAtRest')}
            <YBButton
              dataTestId="edit-security-at-rest-button"
              variant="ghost"
              startIcon={<EditIcon />}
              onClick={() => setEarModalOpen(true)}
              disabled={earModalOpen || isLegacyUniverseLoading || !universeUUID}
            >
              {t('edit', { keyPrefix: 'common' })}
            </YBButton>
          </StyledHeader>
          <StyledContent>
            <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
              <div>
                <span className="header">{t('encryption')}</span>
                <span className="value sameline nogap">
                  {isLegacyUniverseLoading ? (
                    <CircularProgress size={18} />
                  ) : (
                    <>
                      {t(encryptionAtRestEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                      {encryptionAtRestEnabled ? <CheckedIcon /> : <DisabledIcon />}
                    </>
                  )}
                </span>
              </div>
            </StyledInfoRow>
          </StyledContent>
        </StyledPanel>
      </Box>
      {universeData?.spec?.encryption_in_transit_spec && (
        <EncryptionInTransit
          open={eitModalOpen}
          onClose={() => {
            setEitModalOpen(false);
          }}
          isItKubernetesUniverse={isItKubernetesUniverse}
          v2Spec={{
            universeUUID: universeUUID || '',
            eitSpec: universeData?.spec?.encryption_in_transit_spec
          }}
        />
      )}
      {legacyUniverse && universeUUID && (
        <EncryptionAtRest
          open={earModalOpen}
          onClose={() => {
            setEarModalOpen(false);
            void queryClient.invalidateQueries([QUERY_KEY.fetchUniverse, universeUUID]);
          }}
          universeDetails={legacyUniverse}
        />
      )}
    </FormProvider>
  );
};
