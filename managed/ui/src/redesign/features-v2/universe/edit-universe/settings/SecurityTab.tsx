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
import { getClusterByType, useEditUniverseContext, useIsUniverseReady } from '../EditUniverseUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType } from '@app/redesign/helpers/dtos';
import { EditNetworkAcessModal } from '../edit-security/EditNetworkAcessModal';

import Checked from '@app/redesign/assets/check-new.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import Disabled from '@app/redesign/assets/revoke.svg';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';

const { styled, Box, CircularProgress } = mui;

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
  const [networkModalOpen, setNetworkModalOpen] = useState(false);
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

  const isPublicIPAssigned = !!universeData?.spec?.networking_spec?.assign_public_ip;
  const isIPV6Enabled = !!universeData?.spec?.networking_spec?.enable_ipv6;
  const isK8sPublicIPAssigned =
    primaryCluster?.networking_spec?.enable_exposing_service === 'EXPOSED';

  const isItKubernetesUniverse = providerCode === CloudType.kubernetes;

  const isUniverseReady = useIsUniverseReady();
  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
        {providerCode !== CloudType.onprem && (
          <StyledPanel>
            <StyledHeader
              sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
            >
              {t('networkAccess')}
              <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl>
                <YBButton
                  dataTestId="edit-network-access-button"
                  variant="ghost"
                  startIcon={<EditIcon />}
                  onClick={() => {
                    setNetworkModalOpen(true);
                  }}
                  disabled={!isUniverseReady}
                >
                  {t('edit', { keyPrefix: 'common' })}
                </YBButton>
              </RbacValidator>
            </StyledHeader>
            <StyledContent>
              <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
                {[CloudType.aws, CloudType.gcp, CloudType.azu].includes(providerCode) && (
                  <div>
                    <span className="header">{t('publicIP')}</span>
                    <span className="value sameline nogap">
                      {t(isPublicIPAssigned ? 'assigned' : 'notAssigned', { keyPrefix: 'common' })}
                      {isPublicIPAssigned ? <CheckedIcon /> : <DisabledIcon />}
                    </span>
                  </div>
                )}
                {providerCode === CloudType.kubernetes && (
                  <>
                    <div>
                      <span className="header">{t('ipv6')}</span>
                      <span className="value sameline nogap">
                        {t(isIPV6Enabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                        {isIPV6Enabled ? <CheckedIcon /> : <DisabledIcon />}
                      </span>
                    </div>
                    <div>
                      <span className="header">{t('publicIP')}</span>
                      <span className="value sameline nogap">
                        {t(isK8sPublicIPAssigned ? 'assigned' : 'notAssigned', {
                          keyPrefix: 'common'
                        })}
                        {isK8sPublicIPAssigned ? <CheckedIcon /> : <DisabledIcon />}
                      </span>
                    </div>
                  </>
                )}
              </StyledInfoRow>
            </StyledContent>
          </StyledPanel>
        )}
        <StyledPanel>
          <StyledHeader
            sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
          >
            {t('encryptionInTransit')}
            <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_UNIVERSE_TLS} isControl>
              <YBButton
                dataTestId="edit-security-transit-button"
                variant="ghost"
                startIcon={<EditIcon />}
                onClick={() => setEitModalOpen(true)}
                disabled={eitModalOpen || !isUniverseReady}
              >
                {t('edit', { keyPrefix: 'common' })}
              </YBButton>
            </RbacValidator>
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
            <RbacValidator accessRequiredOn={ApiPermissionMap.MODIFY_UNIVERSE_TLS} isControl>
              <YBButton
                dataTestId="edit-security-at-rest-button"
                variant="ghost"
                startIcon={<EditIcon />}
                onClick={() => setEarModalOpen(true)}
                disabled={
                  earModalOpen || isLegacyUniverseLoading || !universeUUID || !isUniverseReady
                }
              >
                {t('edit', { keyPrefix: 'common' })}
              </YBButton>
            </RbacValidator>
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
      <EditNetworkAcessModal open={networkModalOpen} onClose={() => setNetworkModalOpen(false)} />
    </FormProvider>
  );
};
