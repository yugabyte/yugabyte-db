import { useState } from 'react';
import { isEmpty, drop } from 'lodash';
import { useTranslation, Trans } from 'react-i18next';
import { mui, YBButton, YBTag, YBTab, YBTabs, YBTable, YBTooltip } from '@yugabyte-ui-library/core';
import { useToggle } from 'react-use';
import { toast } from 'react-toastify';
import {
  StyledContent,
  StyledHeader,
  StyledInfoRow,
  StyledPanel
} from '../../create-universe/components/DefaultComponents';
import { getClusterByType, useEditUniverseContext, useIsUniverseReady } from '../EditUniverseUtils';
import {
  getAccessiblePorts,
  mapAPIPortsKeys
} from '../../create-universe/utils/createUniversePayload';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  EditAdvancedSettingsModal,
  EditNetworkPortsModal,
  EditNodeAcessModal,
  EditUserTagsModal
} from '../edit-advanced';
import { K8sHelmOverridesModal } from '../../create-universe/fields/k8s-helmoverrides/K8sHelmOverridesModal';
import { useYBToast } from '../../create-universe/helpers/ToastUtils';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';
import { transformInstanceTags } from '../../../../features/universe/universe-form/utils/helpers';
import { StyledLink, StyledEmptyState } from '../../create-universe/components/DefaultComponents';
import { CloudType } from '@app/redesign/features/universe/universe-form/utils/dto';
import { useEditKubernetesOverrides } from '@app/v2/api/universe/universe';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { createErrorMessage } from '@app/utils/ObjectUtils';
import { isCloudVendorCloudType } from '@app/components/configRedesign/providerRedesign/utils';
//icons
import Checked from '@app/redesign/assets/check-new.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import Disabled from '@app/redesign/assets/revoke.svg';
import CopyIcon from '@app/redesign/assets/copy_blue.svg';
import AddCircleIcon from '@app/redesign/assets/add-circle-blue.svg';

const { styled, Box, Typography } = mui;

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

export enum AdvancedTabs {
  PROXY = 'proxy',
  OTHER = 'other'
}

interface NodeFieldProps {
  label: string;
  value: string;
}

export const NodeField = ({ label, value }: NodeFieldProps) => {
  const toast = useYBToast();

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '4px', minWidth: '200px' }}>
      <Typography variant="button">{label}</Typography>
      <Box sx={{ display: 'flex', flexDirection: 'row', gap: '4px', alignItems: 'center' }}>
        <Typography variant="body2">{value ?? '-'}</Typography>
        {!isEmpty(value) && (
          <CopyIcon
            onClick={() => {
              navigator.clipboard.writeText(value ?? '');
              toast.success('Copied');
            }}
          />
        )}
      </Box>
    </Box>
  );
};

export const NetworkPortsContent = () => {
  const { universeData } = useEditUniverseContext();
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings.deployPortsFeild'
  });

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const enableYSQL = universeData?.spec?.ysql?.enable;
  const enableYCQL = universeData?.spec?.ycql?.enable;
  const enableCP = universeData?.spec?.ysql?.enable_connection_pooling;

  const communicationPorts = universeData?.spec?.networking_spec?.communication_ports;
  const PORTGROUPS = getAccessiblePorts(enableYSQL, enableYCQL, providerCode, enableCP, t);

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: '24px' }}>
      {PORTGROUPS.map((pg) => {
        return (
          <Box
            sx={{ display: 'flex', flexDirection: 'row', alignItems: 'center', gap: '56px' }}
            key={pg.name}
          >
            <Box
              sx={{
                display: 'flex',
                borderRight: '1px solid #D7DEE4',
                width: '150px',
                height: '32px',
                alignItems: 'center'
              }}
            >
              <Typography variant="body1" sx={{ color: '#4E5F6D' }}>
                {pg.name}
              </Typography>
            </Box>
            {pg.PORTS_LIST.map((item) => {
              return (
                <Box sx={{ display: 'flex', flexDirection: 'column', gap: '4px', width: '200px' }}>
                  <Typography variant="button" sx={{ color: '#6D7C88' }}>
                    {t(item.id)}
                  </Typography>
                  <Typography variant="body2" sx={{ color: '#0B1117' }}>
                    {communicationPorts[mapAPIPortsKeys()[item.id]]}
                  </Typography>
                </Box>
              );
            })}
          </Box>
        );
      })}
    </Box>
  );
};

const EditK8sHelmOverrides = () => {
  const [openHelmOverridesModal, setHelmOverridesModal] = useState(false);
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.otherAdvancedSettings'
  });
  const editOverrides = useEditKubernetesOverrides();
  const { universeData } = useEditUniverseContext();
  const isUniverseReady = useIsUniverseReady();
  const uniUUID = universeData?.info?.universe_uuid ?? '';
  const dbVersion = universeData?.spec?.yb_software_version;
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(uniUUID);
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const placementSpec = primaryCluster?.placement_spec;
  const universeOverrides = primaryCluster?.provider_spec?.helm_overrides ?? '';
  const azOverrides: any = primaryCluster?.provider_spec?.az_helm_overrides || {};
  const overrideExists = !(isEmpty(universeOverrides) && isEmpty(azOverrides));

  const handleClose = () => {
    setHelmOverridesModal(false);
  };

  const handleSubmit = (universeOverrides: string, azOverrides: Record<string, string>) => {
    editOverrides.mutate(
      {
        uniUUID,
        data: { overrides: universeOverrides, az_overrides: azOverrides }
      },
      {
        onSuccess: (resp) => {
          handleEditUniverseSuccess(resp.task_uuid);
          handleClose();
        },
        onError: (error: unknown) => {
          toast.error(createErrorMessage(error));
          handleClose();
        }
      }
    );
  };

  return (
    <StyledPanel sx={{ marginTop: '24px' }}>
      <StyledHeader sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        {t('k8sOverrides')}
        {overrideExists && (
          <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl>
            <YBButton
              dataTestId="edit-kubernetes-overrides-button"
              variant="ghost"
              startIcon={<EditIcon />}
              onClick={() => setHelmOverridesModal(true)}
              disabled={!isUniverseReady}
            >
              {t('edit', { keyPrefix: 'common' })}
            </YBButton>
          </RbacValidator>
        )}
      </StyledHeader>
      <StyledContent>
        {!overrideExists ? (
          <StyledEmptyState>
            <Typography variant="body2" sx={{ color: '#4E5F6D' }}>
              {t('overrideInfo')}
            </Typography>
            <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl>
              <YBButton
                variant="secondary"
                dataTestId="add-gflags-button"
                startIcon={<AddCircleIcon />}
                sx={{ mt: 2 }}
                disabled={!isUniverseReady}
                onClick={() => setHelmOverridesModal(true)}
              >
                {t('addHelmOverrides')}
              </YBButton>
            </RbacValidator>
          </StyledEmptyState>
        ) : (
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
            <Typography variant="button">{t('helmOverrides')}</Typography>
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
          </Box>
        )}
      </StyledContent>
      {openHelmOverridesModal && (
        <K8sHelmOverridesModal
          initialValues={{ azOverrides, universeOverrides }}
          placementSpec={placementSpec}
          onClose={handleClose}
          onSubmit={handleSubmit}
          dbVersion={dbVersion}
          open={openHelmOverridesModal}
        />
      )}
    </StyledPanel>
  );
};

export const AdvancedTab = () => {
  const { universeData } = useEditUniverseContext();
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.advanced'
  });
  const [selectedTab, setSelectedTab] = useState(AdvancedTabs.PROXY);
  const [isEditAdvancedSettingsModalVisible, setEditAdvancedSettingsModalVisible] =
    useToggle(false);
  const [isUserTagsModalOpen, setUserTagsModalOpen] = useState(false);
  const [isNodeModalOpen, setNodeModalOpen] = useState(false);
  const [isNetworkPortsModalOpen, setNetworkPortsModalOpen] = useState(false);
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const networking_spec = primaryCluster?.networking_spec;
  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const isUniverseReady = useIsUniverseReady();
  const accessKeyValue = primaryCluster?.provider_spec?.access_key_code;
  const awsArnString = primaryCluster?.provider_spec?.aws_instance_profile;
  const userTags = transformInstanceTags(primaryCluster?.instance_tags);
  const isProxyEnabled =
    networking_spec?.proxy_config?.http_proxy || networking_spec?.proxy_config?.https_proxy;

  const getNoProxyList = () => {
    if (!networking_spec?.proxy_config?.no_proxy_list?.length) {
      return '-';
    }

    return (
      <>
        {networking_spec?.proxy_config?.no_proxy_list?.[0]}
        <YBTag size="small" variant="light">
          <YBTooltip
            title={
              <Box sx={{ display: 'flex', flexDirection: 'column', color: '#4E5F6D' }}>
                <ul style={{ listStyleType: 'disc', paddingInlineStart: '20px' }}>
                  {drop(networking_spec?.proxy_config?.no_proxy_list, 1).map((nl) => (
                    <li>
                      <Typography sx={{ lineHeight: '20px' }} variant="subtitle1">
                        {nl}
                      </Typography>
                    </li>
                  ))}
                </ul>
              </Box>
            }
          >
            <span>+{(networking_spec?.proxy_config?.no_proxy_list?.length ?? 1) - 1}</span>
          </YBTooltip>
        </YBTag>
      </>
    );
  };
  return (
    <Box sx={{ width: '100%' }}>
      <YBTabs value={selectedTab} onChange={(_event, newValue) => setSelectedTab(newValue)}>
        <YBTab value={AdvancedTabs.PROXY} label={'Proxy Settings'} />
        <YBTab value={AdvancedTabs.OTHER} label={'Other Advanced Settings'} />
      </YBTabs>
      {selectedTab === AdvancedTabs.PROXY && (
        <StyledPanel sx={{ marginTop: '24px' }}>
          <StyledHeader
            sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
          >
            {t('proxyConfiguration')}
            {isProxyEnabled && (
              <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl>
                <YBButton
                  dataTestId="edit-security-transit-button"
                  variant="ghost"
                  startIcon={<EditIcon />}
                  onClick={() => {
                    setEditAdvancedSettingsModalVisible(true);
                  }}
                  disabled={!isUniverseReady}
                >
                  {t('edit', { keyPrefix: 'common' })}
                </YBButton>
              </RbacValidator>
            )}
          </StyledHeader>
          {
            <StyledContent sx={{ gap: '24px' }}>
              {isProxyEnabled ? (
                <>
                  <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
                    <div>
                      <span className="header">{t('proxyServer')}</span>
                      <span className="value sameline nogap">
                        {t(networking_spec?.proxy_config ? 'enabled' : 'disabled', {
                          keyPrefix: 'common'
                        })}
                        {networking_spec?.proxy_config ? <CheckedIcon /> : <DisabledIcon />}
                      </span>
                    </div>
                  </StyledInfoRow>
                  <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
                    <div style={{ width: '300px' }}>
                      <span className="header">{t('secureWebProxy')}</span>
                      <span className="value ">
                        {networking_spec?.proxy_config?.https_proxy ?? '-'}
                      </span>
                    </div>
                    <div style={{ width: '300px' }}>
                      <span className="header">{t('webProxy')}</span>
                      <span className="value ">
                        {networking_spec?.proxy_config?.http_proxy ?? '-'}
                      </span>
                    </div>
                    <div style={{ width: '300px' }}>
                      <span className="header">{t('bypassProxyList')}</span>
                      <span className="value sameline">{getNoProxyList()}</span>
                    </div>
                  </StyledInfoRow>
                </>
              ) : (
                <StyledEmptyState>
                  <Typography variant="body2" sx={{ color: '#4E5F6D' }}>
                    <Trans t={t} i18nKey={'proxyHelper'} components={{ a: <StyledLink /> }} />
                  </Typography>
                  <RbacValidator
                    accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER}
                    isControl
                  >
                    <YBButton
                      variant="secondary"
                      dataTestId="add-gflags-button"
                      sx={{ mt: 2 }}
                      disabled={!isUniverseReady}
                      onClick={() => {
                        setEditAdvancedSettingsModalVisible(true);
                      }}
                    >
                      {t('enableProxyServer')}
                    </YBButton>
                  </RbacValidator>
                </StyledEmptyState>
              )}
            </StyledContent>
          }
          <EditAdvancedSettingsModal
            visible={isEditAdvancedSettingsModalVisible}
            onClose={() => setEditAdvancedSettingsModalVisible(false)}
          />
        </StyledPanel>
      )}
      {selectedTab === AdvancedTabs.OTHER && (
        <>
          {providerCode !== CloudType.kubernetes && (
            <StyledPanel sx={{ marginTop: '24px' }}>
              <StyledHeader
                sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
              >
                {t('nodeAccess')}
                {providerCode === CloudType.aws && (
                  <RbacValidator
                    accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER}
                    isControl
                  >
                    <YBButton
                      dataTestId="edit-security-transit-button"
                      variant="ghost"
                      startIcon={<EditIcon />}
                      onClick={() => setNodeModalOpen(true)}
                      disabled={!isUniverseReady}
                    >
                      {t('edit', { keyPrefix: 'common' })}
                    </YBButton>
                  </RbacValidator>
                )}
              </StyledHeader>
              <StyledContent>
                <Box
                  sx={{ display: 'flex', flexDirection: 'row', gap: '72px', alignItems: 'center' }}
                >
                  <NodeField label={t('sshKey')} value={accessKeyValue} />
                  {providerCode === CloudType.aws && (
                    <NodeField label={t('awsArn')} value={awsArnString} />
                  )}
                </Box>
              </StyledContent>
            </StyledPanel>
          )}
          {providerCode !== CloudType.kubernetes && (
            <StyledPanel sx={{ marginTop: '24px' }}>
              <StyledHeader
                sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
              >
                {t('networkPorts')}
                <RbacValidator
                  accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER}
                  isControl
                >
                  <YBButton
                    dataTestId="edit-security-transit-button"
                    variant="ghost"
                    startIcon={<EditIcon />}
                    onClick={() => setNetworkPortsModalOpen(true)}
                    disabled={!isUniverseReady}
                  >
                    {t('edit', { keyPrefix: 'common' })}
                  </YBButton>
                </RbacValidator>
              </StyledHeader>
              <StyledContent>
                <NetworkPortsContent />
              </StyledContent>
            </StyledPanel>
          )}
          {providerCode === CloudType.kubernetes && <EditK8sHelmOverrides />}
          {isCloudVendorCloudType(providerCode) && (
            <StyledPanel sx={{ marginTop: '24px' }}>
              <StyledHeader
                sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
              >
                {t('userTagsTitle')}
                {userTags.length > 0 && (
                  <RbacValidator
                    accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER}
                    isControl
                  >
                    <YBButton
                      dataTestId="edit-user-tags-button"
                      variant="ghost"
                      startIcon={<EditIcon />}
                      onClick={() => {
                        setUserTagsModalOpen(true);
                      }}
                      disabled={!isUniverseReady}
                    >
                      {t('edit', { keyPrefix: 'common' })}
                    </YBButton>
                  </RbacValidator>
                )}
              </StyledHeader>
              <StyledContent sx={{ paddingTop: '0px' }}>
                {userTags.length <= 0 ? (
                  <StyledEmptyState>
                    <Typography variant="body2" sx={{ color: '#4E5F6D' }}>
                      <Trans t={t} i18nKey={'userTagTooltip'} components={{ a: <StyledLink /> }} />
                    </Typography>
                    <RbacValidator
                      accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER}
                      isControl
                    >
                      <YBButton
                        variant="secondary"
                        dataTestId="add-gflags-button"
                        startIcon={<AddCircleIcon />}
                        sx={{ mt: 2 }}
                        disabled={!isUniverseReady}
                        onClick={() => {
                          setUserTagsModalOpen(true);
                        }}
                      >
                        {t('addTags')}
                      </YBButton>
                    </RbacValidator>
                  </StyledEmptyState>
                ) : (
                  <YBTable
                    columns={[
                      { accessorKey: 'name', header: t('userTagName') },
                      { accessorKey: 'value', header: t('userTagValue') }
                    ]}
                    data={userTags}
                    options={{
                      pagination: false
                    }}
                  />
                )}
              </StyledContent>
            </StyledPanel>
          )}
        </>
      )}
      <EditUserTagsModal
        open={isUserTagsModalOpen}
        onClose={() => {
          setUserTagsModalOpen(false);
        }}
      />
      <EditNodeAcessModal
        open={isNodeModalOpen}
        onClose={() => {
          setNodeModalOpen(false);
        }}
      />
      <EditNetworkPortsModal
        open={isNetworkPortsModalOpen}
        onClose={() => {
          setNetworkPortsModalOpen(false);
        }}
      />
    </Box>
  );
};
