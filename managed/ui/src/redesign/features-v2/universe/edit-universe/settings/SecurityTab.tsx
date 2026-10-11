import { useCallback, useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useQuery, useQueryClient } from 'react-query';
import { mui, YBButton } from '@yugabyte-ui-library/core';
import {
  StyledContent,
  StyledCardHeader,
  StyledInfoRow,
  StyledPanel
} from '../../create-universe/components/DefaultComponents';
import { EncryptionInTransit } from '@app/redesign/features/universe/universe-actions/encryption-in-transit/EncryptionInTransit';
import { EncryptionAtRest } from '@app/redesign/features/universe/universe-actions/encryption-at-rest/EncryptionAtRest';
import { api, QUERY_KEY } from '@app/redesign/utils/api';
import { FormProvider, useForm } from 'react-hook-form';
import { SecuritySettingsProps } from '../../create-universe/steps/security-settings/dtos';
import {
  getClusterByType,
  useEditUniverseContext,
  useIsUniverseReady,
  withUniverseResource
} from '../EditUniverseUtils';
import { getGetUniverseQueryKey } from '@app/v2/api/universe/universe';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { CloudType } from '@app/redesign/helpers/dtos';
import { isCloudVendorCloudType } from '@app/components/configRedesign/providerRedesign/utils';
import { EditNetworkAcessModal } from '../edit-security/EditNetworkAcessModal';
import { getPrimaryCluster } from '@app/utils/universeUtilsTyped';

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

  const legacyPrimaryCluster = legacyUniverse?.universeDetails?.clusters
    ? getPrimaryCluster(legacyUniverse.universeDetails.clusters)
    : undefined;
  const nodeToNodeEnabled = !!legacyPrimaryCluster?.userIntent?.enableNodeToNodeEncrypt;
  const clientToNodeEnabled = !!legacyPrimaryCluster?.userIntent?.enableClientToNodeEncrypt;

  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const isPublicIPAssigned = !!universeData?.spec?.networking_spec?.assign_public_ip;
  const isIPV6Enabled = !!universeData?.spec?.networking_spec?.enable_ipv6;
  const isK8sPublicIPAssigned =
    primaryCluster?.networking_spec?.enable_exposing_service === 'EXPOSED';

  const isItKubernetesUniverse = providerCode === CloudType.kubernetes;

  const isUniverseReady = useIsUniverseReady();

  const invalidateUniverseQueries = useCallback(() => {
    if (!universeUUID) return;
    void queryClient.invalidateQueries([QUERY_KEY.fetchUniverse, universeUUID]);
    void queryClient.invalidateQueries(getGetUniverseQueryKey(universeUUID));
    void queryClient.invalidateQueries(QUERY_KEY.getKMSHistory);
  }, [queryClient, universeUUID]);

  // TLS / set_key tasks are async; v2 spec updates when the task completes. Refetch the
  // v1 universe so EncryptionInTransit and EncryptionAtRest pick up the new config.
  const v2KmsConfigUuid = universeData?.spec?.encryption_at_rest_spec?.kms_config_uuid;
  const v2Eit = universeData?.spec?.encryption_in_transit_spec;
  const isFirstSecuritySync = useRef(true);
  useEffect(() => {
    if (isFirstSecuritySync.current) {
      isFirstSecuritySync.current = false;
      return;
    }
    invalidateUniverseQueries();
  }, [
    v2KmsConfigUuid,
    v2Eit?.enable_node_to_node_encrypt,
    v2Eit?.enable_client_to_node_encrypt,
    v2Eit?.root_ca,
    v2Eit?.client_root_ca,
    v2Eit?.root_and_client_root_ca_same,
    invalidateUniverseQueries
  ]);

  return (
    <FormProvider {...methods}>
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
        {providerCode !== CloudType.onprem && (
          <StyledPanel>
            <StyledCardHeader>
              {t('networkAccess')}
              <RbacValidator
                accessRequiredOn={withUniverseResource(
                  ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER,
                  universeUUID
                )}
                isControl
              >
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
            </StyledCardHeader>
            <StyledContent>
              <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
                {isCloudVendorCloudType(providerCode) && (
                  <div>
                    <span className="header">{t('publicIP')}</span>
                    <span className="value sameline gap4">
                      {t(isPublicIPAssigned ? 'assigned' : 'notAssigned', { keyPrefix: 'common' })}
                      {isPublicIPAssigned ? <CheckedIcon /> : <DisabledIcon />}
                    </span>
                  </div>
                )}
                {providerCode === CloudType.kubernetes && (
                  <>
                    <div>
                      <span className="header">{t('ipv6')}</span>
                      <span className="value sameline gap4">
                        {t(isIPV6Enabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                        {isIPV6Enabled ? <CheckedIcon /> : <DisabledIcon />}
                      </span>
                    </div>
                    <div>
                      <span className="header">{t('publicIP')}</span>
                      <span className="value sameline gap4">
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
          <StyledCardHeader>
            {t('encryptionInTransit')}
            <RbacValidator
              accessRequiredOn={withUniverseResource(
                ApiPermissionMap.MODIFY_UNIVERSE_TLS,
                universeUUID
              )}
              isControl
            >
              <YBButton
                dataTestId="edit-security-transit-button"
                variant="ghost"
                startIcon={<EditIcon />}
                onClick={() => setEitModalOpen(true)}
                disabled={
                  eitModalOpen || isLegacyUniverseLoading || !universeUUID || !isUniverseReady
                }
              >
                {t('edit', { keyPrefix: 'common' })}
              </YBButton>
            </RbacValidator>
          </StyledCardHeader>
          <StyledContent>
            <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
              <div>
                <span className="header">{t('nodeToNode')}</span>
                <span className="value sameline gap4">
                  {isLegacyUniverseLoading ? (
                    <CircularProgress size={18} />
                  ) : (
                    <>
                      {t(nodeToNodeEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                      {nodeToNodeEnabled ? <CheckedIcon /> : <DisabledIcon />}
                    </>
                  )}
                </span>
              </div>
              <div>
                <span className="header">{t('clientToNode')}</span>
                <span className="value sameline gap4">
                  {isLegacyUniverseLoading ? (
                    <CircularProgress size={18} />
                  ) : (
                    <>
                      {t(clientToNodeEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                      {clientToNodeEnabled ? <CheckedIcon /> : <DisabledIcon />}
                    </>
                  )}
                </span>
              </div>
            </StyledInfoRow>
          </StyledContent>
        </StyledPanel>
        <StyledPanel>
          <StyledCardHeader>
            {t('encryptionAtRest')}
            <RbacValidator
              accessRequiredOn={withUniverseResource(
                ApiPermissionMap.MODIFY_UNIVERSE_TLS,
                universeUUID
              )}
              isControl
            >
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
          </StyledCardHeader>
          <StyledContent>
            <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
              <div>
                <span className="header">{t('encryption')}</span>
                <span className="value sameline gap4">
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
      {legacyUniverse && universeUUID && (
        <EncryptionInTransit
          open={eitModalOpen}
          onClose={() => {
            setEitModalOpen(false);
            invalidateUniverseQueries();
          }}
          universe={legacyUniverse}
          isItKubernetesUniverse={isItKubernetesUniverse}
        />
      )}
      {legacyUniverse && universeUUID && (
        <EncryptionAtRest
          open={earModalOpen}
          onClose={() => {
            setEarModalOpen(false);
            invalidateUniverseQueries();
          }}
          universeDetails={legacyUniverse}
        />
      )}
      <EditNetworkAcessModal open={networkModalOpen} onClose={() => setNetworkModalOpen(false)} />
    </FormProvider>
  );
};
