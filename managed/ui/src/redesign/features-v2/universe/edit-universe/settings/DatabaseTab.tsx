import { useState } from 'react';
import { mui, YBButton, YBButtonGroup, YBDropdown } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { useForm } from 'react-hook-form';
import { useQuery, useQueryClient } from 'react-query';
import { yupResolver } from '@hookform/resolvers/yup';
import {
  StyledContent,
  StyledCardHeader,
  StyledInfoRow,
  StyledPanel
} from '../../create-universe/components/DefaultComponents';

import { KeyboardArrowDown } from '@material-ui/icons';
import {
  convertGFlagApiRespToFormValues,
  getClusterByType,
  useEditUniverseContext,
  useIsUniverseReady
} from '../EditUniverseUtils';

import { EditGflagsModal } from '@app/redesign/features/universe/universe-actions/edit-gflags/EditGflags';
import { transformToEditFlagsForm } from '@app/redesign/features/universe/universe-actions/edit-gflags/GflagHelper';
import { EditConnectionPoolModal } from '@app/redesign/features/universe/universe-actions/edit-connection-pool/EditConnectionPoolModal';
import { EditPGCompatibilityModal } from '@app/redesign/features/universe/universe-actions/edit-pg-compatibility/EditPGCompatibilityModal';
import { EnableYSQLModal } from '@app/redesign/features/universe/universe-actions/edit-ysql-ycql/EnableYSQLModal';
import { EnableYCQLModal } from '@app/redesign/features/universe/universe-actions/edit-ysql-ycql/EnableYCQLModal';
import { api, QUERY_KEY } from '@app/redesign/utils/api';
import {
  api as universeFormApi,
  QUERY_KEY as universeFormQueryKey
} from '@app/redesign/features/universe/universe-form/utils/api';
import { RunTimeConfigEntry } from '@app/redesign/features/universe/universe-form/utils/dto';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import { getGetUniverseQueryKey } from '@app/v2/api/universe/universe';
import { CloudType } from '@app/redesign/helpers/dtos';

import {
  ClusterGFlagsAllOfGflagGroupsItem,
  ClusterSpecClusterType
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { ReadOnlyGflagTable } from '@app/redesign/features/universe/universe-form/form/sections/gflags/ReadOnlyGflagsModal';
import { DatabaseSettingsProps } from '../../create-universe/steps/database-settings/dtos';
import { DatabaseValidationSchema } from '../../create-universe/steps/database-settings/ValidationSchema';

import Checked from '@app/redesign/assets/check-new.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import Disabled from '@app/redesign/assets/revoke.svg';
import AddCircleIcon from '@app/redesign/assets/add-circle-blue.svg';
import { getAsyncCluster } from '@app/redesign/features/universe/universe-form/utils/helpers';
import { RbacValidator } from '@app/redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '@app/redesign/features/rbac/ApiAndUserPermMapping';

const { Box, MenuItem, styled } = mui;

const GFLAG_CLUSTER_TYPE = {
  PRIMARY: 'primary',
  READ_REPLICA: 'readReplica'
} as const;

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

export const DatabaseTab = () => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.database' });
  const { t: tUniverseForm } = useTranslation('translation');
  const queryClient = useQueryClient();
  const { universeData } = useEditUniverseContext();

  const [connectionPoolModalOpen, setConnectionPoolModalOpen] = useState(false);
  const [pgCompatibilityModalOpen, setPgCompatibilityModalOpen] = useState(false);
  const [ysqlModalOpen, setYsqlModalOpen] = useState(false);
  const [ycqlModalOpen, setYcqlModalOpen] = useState(false);
  const [gflagsModalOpen, setGflagsModalOpen] = useState(false);
  const [selectedGflagCluster, setSelectedGflagCluster] = useState<string>(
    GFLAG_CLUSTER_TYPE.PRIMARY
  );
  const isUniverseReady = useIsUniverseReady();

  const universeUUID = universeData?.info?.universe_uuid;

  const { data: legacyUniverse, isLoading: isLegacyUniverseLoading } = useQuery(
    [QUERY_KEY.fetchUniverse, universeUUID],
    () => api.fetchUniverse(universeUUID!),
    { enabled: !!universeUUID }
  );

  const { data: runtimeConfigs } = useQuery(
    [universeFormQueryKey.fetchCustomerRunTimeConfigs],
    () => universeFormApi.fetchRunTimeConfigs(true)
  );

  const isAuthEnforced = !!(
    runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.IS_UNIVERSE_AUTH_ENFORCED
    )?.value === 'true'
  );

  const isGFlagMultilineConfEnabled = !!(
    runtimeConfigs?.configEntries?.find(
      (c: RunTimeConfigEntry) => c.key === RuntimeConfigKey.IS_GFLAG_MULTILINE_ENABLED
    )?.value === 'true'
  );

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const isItKubernetesUniverse = providerCode === CloudType.kubernetes;

  const invalidateUniverseQueries = () => {
    if (universeUUID) {
      void queryClient.invalidateQueries([QUERY_KEY.fetchUniverse, universeUUID]);
      void queryClient.invalidateQueries(getGetUniverseQueryKey(universeUUID));
    }
  };

  const { control } = useForm<DatabaseSettingsProps>({
    resolver: yupResolver(DatabaseValidationSchema()),
    defaultValues: {
      gFlags: convertGFlagApiRespToFormValues(primaryCluster?.gflags)
    },
    mode: 'onChange'
  });

  const ysqlEnabed = universeData?.spec?.ysql?.enable;
  const ysqlAuthEnabled = universeData?.spec?.ysql?.enable_auth;
  const ycqlEnabled = universeData?.spec?.ycql?.enable;
  const ycqlAuthEnabled = universeData?.spec?.ycql?.enable_auth;

  const connectionPoolingEnabled = universeData?.spec?.ysql?.enable_connection_pooling;
  const postgresCompatibilityEnabled = primaryCluster?.gflags?.gflag_groups?.find(
    (g) => g === ClusterGFlagsAllOfGflagGroupsItem.ENHANCED_POSTGRES_COMPATIBILITY
  );

  const databaseVersion = universeData?.spec?.yb_software_version;

  const {
    gFlags: primaryGflags = [],
    asyncGflags = [],
    inheritFlagsFromPrimary = true
  } = legacyUniverse ? transformToEditFlagsForm(legacyUniverse) : {};
  const hasReadReplica = !!(
    legacyUniverse?.universeDetails && getAsyncCluster(legacyUniverse.universeDetails)
  );
  const isPrimaryGflagsView = selectedGflagCluster === GFLAG_CLUSTER_TYPE.PRIMARY;
  const readReplicaSourceGflags = inheritFlagsFromPrimary ? primaryGflags : asyncGflags;
  // Read replicas are TSERVER-only — drop MASTER values and master-only flag rows.
  const readReplicaGflags = readReplicaSourceGflags
    .filter((flag) => flag.TSERVER !== undefined)
    .map(({ MASTER: _master, ...flag }) => flag);
  const displayedGflags = isPrimaryGflagsView ? primaryGflags : readReplicaGflags;
  const hasAnyGflags = primaryGflags.length > 0 || asyncGflags.length > 0;

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
      <StyledPanel>
        <StyledCardHeader>
          {t('interface')}
          <YBDropdown
            sx={{ width: '340px' }}
            dataTestId="edit-ysql-ycql-actions"
            origin={
              <YBButton
                variant="ghost"
                startIcon={<EditIcon />}
                dataTestId="edit-ysql-ycql-actions-button"
                endIcon={<KeyboardArrowDown />}
              >
                {t('edit', { keyPrefix: 'common' })}
              </YBButton>
            }
          >
            <RbacValidator accessRequiredOn={ApiPermissionMap.UNIVERSE_CONFIGURE_YSQL} isControl>
              <MenuItem
                data-test-id="edit-ysql-settings"
                data-testid="edit-ysql-settings"
                disabled={isLegacyUniverseLoading || !legacyUniverse || !isUniverseReady}
                onClick={() => setYsqlModalOpen(true)}
              >
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editYSQLSettings')}
              </MenuItem>
            </RbacValidator>
            <RbacValidator accessRequiredOn={ApiPermissionMap.UNIVERSE_CONFIGURE_YCQL} isControl>
              <MenuItem
                data-test-id="edit-ycql-settings"
                data-testid="edit-ycql-settings"
                disabled={isLegacyUniverseLoading || !legacyUniverse || !isUniverseReady}
                onClick={() => setYcqlModalOpen(true)}
              >
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editYCQLSettings')}
              </MenuItem>
            </RbacValidator>
          </YBDropdown>
        </StyledCardHeader>
        <StyledContent sx={{ gap: '24px' }}>
          <StyledInfoRow sx={{ flexDirection: 'row', gap: '190px' }}>
            <div>
              <span className="header">{t('ysql')}</span>
              <span className="value sameline gap4">
                {t(ysqlEnabed ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {ysqlEnabed ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
            <div>
              <span className="header">{t('ysqlAuthentication')}</span>{' '}
              <span className="value sameline gap4">
                {t(ysqlAuthEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {ysqlAuthEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
          </StyledInfoRow>
          <StyledInfoRow sx={{ flexDirection: 'row', gap: '190px' }}>
            <div>
              <span className="header">{t('ycql')}</span>
              <span className="value sameline gap4">
                {t(ycqlEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {ycqlEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
            <div>
              <span className="header">{t('ycqlAuthentication')}</span>{' '}
              <span className="value sameline gap4">
                {t(ycqlAuthEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {ycqlAuthEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
          </StyledInfoRow>
        </StyledContent>
      </StyledPanel>
      <StyledPanel>
        <StyledCardHeader>
          {t('features')}
          <YBDropdown
            sx={{ width: '340px' }}
            dataTestId="edit-features-actions"
            origin={
              <YBButton
                variant="ghost"
                startIcon={<EditIcon />}
                dataTestId="edit-features-actions-button"
                endIcon={<KeyboardArrowDown />}
              >
                {t('edit', { keyPrefix: 'common' })}
              </YBButton>
            }
          >
            <RbacValidator accessRequiredOn={ApiPermissionMap.UNIVERSE_CONFIGURE_YSQL} isControl>
              <MenuItem
                data-test-id="edit-pooling-settings"
                data-testid="edit-pooling-settings"
                disabled={isLegacyUniverseLoading || !legacyUniverse || !isUniverseReady}
                onClick={() => setConnectionPoolModalOpen(true)}
              >
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editConnectionPooling')}
              </MenuItem>
            </RbacValidator>
            <RbacValidator accessRequiredOn={ApiPermissionMap.UNIVERSE_CONFIGURE_YSQL} isControl>
              <MenuItem
                data-test-id="edit-postgres-compatibility-settings"
                data-testid="edit-postgres-compatibility-settings"
                disabled={isLegacyUniverseLoading || !legacyUniverse || !isUniverseReady}
                onClick={() => setPgCompatibilityModalOpen(true)}
              >
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editPostgresCompatibility')}
              </MenuItem>
            </RbacValidator>
          </YBDropdown>
        </StyledCardHeader>
        <StyledContent sx={{ gap: '24px' }}>
          <StyledInfoRow>
            <div>
              <span className="header">{t('connectionPooling')}</span>
              <span className="value sameline gap4">
                {t(connectionPoolingEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {connectionPoolingEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
          </StyledInfoRow>
          <StyledInfoRow>
            <div>
              <span className="header">{t('postgresCompatibility')}</span>
              <span className="value sameline gap4">
                {t(postgresCompatibilityEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {postgresCompatibilityEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
          </StyledInfoRow>
        </StyledContent>
      </StyledPanel>
      <StyledPanel>
        <StyledCardHeader sx={{ padding: !hasAnyGflags ? '24px' : '26px 24px' }}>
          {t('advancedConfigFlags')}
          {hasAnyGflags && (
            <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl>
              <YBButton
                dataTestId="edit-gflags-button"
                variant="ghost"
                startIcon={<EditIcon />}
                disabled={isLegacyUniverseLoading || !legacyUniverse || !isUniverseReady}
                onClick={() => setGflagsModalOpen(true)}
              >
                {t('edit', { keyPrefix: 'common' })}
              </YBButton>
            </RbacValidator>
          )}
        </StyledCardHeader>
        <StyledContent sx={{ gap: '16px' }}>
          {hasReadReplica && hasAnyGflags && (
            <Box display="flex" justifyContent="flex-end">
              <YBButtonGroup
                size="medium"
                dataTestId="gflags-cluster-type-button-group"
                value={selectedGflagCluster}
                buttons={[
                  {
                    value: GFLAG_CLUSTER_TYPE.PRIMARY,
                    label: tUniverseForm('universeForm.gFlags.primaryTab'),
                    onClick: () => setSelectedGflagCluster(GFLAG_CLUSTER_TYPE.PRIMARY),
                    buttonProps: {
                      dataTestId: 'gflags-primary-cluster-button'
                    }
                  },
                  {
                    value: GFLAG_CLUSTER_TYPE.READ_REPLICA,
                    label: tUniverseForm('universeForm.gFlags.rrTab'),
                    onClick: () => setSelectedGflagCluster(GFLAG_CLUSTER_TYPE.READ_REPLICA),
                    buttonProps: {
                      dataTestId: 'gflags-read-replica-button'
                    }
                  }
                ]}
              />
            </Box>
          )}
          {!hasAnyGflags ? (
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'column',
                height: '168px',
                width: '100%',
                justifyContent: 'center',
                alignItems: 'center',
                bgcolor: '#F2F6FF',
                border: '1px dashed #CBDBFF',
                borderRadius: '8px',
                color: '#4E5F6D',
                fontSize: '13px'
              }}
            >
              {t('noGflagsAdded')} <br />
              <RbacValidator accessRequiredOn={ApiPermissionMap.EDIT_V2_UNIVERSE_CLUSTER} isControl>
                <YBButton
                  variant="secondary"
                  dataTestId="add-gflags-button"
                  startIcon={<AddCircleIcon />}
                  sx={{ mt: 2 }}
                  disabled={isLegacyUniverseLoading || !legacyUniverse || !isUniverseReady}
                  onClick={() => setGflagsModalOpen(true)}
                >
                  {t('addFlag')}
                </YBButton>
              </RbacValidator>
            </Box>
          ) : displayedGflags.length <= 0 ? (
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'column',
                height: '168px',
                width: '100%',
                justifyContent: 'center',
                alignItems: 'center',
                bgcolor: '#F2F6FF',
                border: '1px dashed #CBDBFF',
                borderRadius: '8px',
                color: '#4E5F6D',
                fontSize: '13px'
              }}
            >
              {t('noGflagsAdded')}
            </Box>
          ) : (
            <ReadOnlyGflagTable gFlags={displayedGflags} isPrimary={isPrimaryGflagsView} />
          )}
        </StyledContent>
      </StyledPanel>
      {legacyUniverse && universeUUID && (
        <>
          <EditConnectionPoolModal
            open={connectionPoolModalOpen}
            onClose={() => {
              setConnectionPoolModalOpen(false);
              invalidateUniverseQueries();
            }}
            universeData={legacyUniverse}
            isItKubernetesUniverse={isItKubernetesUniverse}
          />
          <EditPGCompatibilityModal
            open={pgCompatibilityModalOpen}
            onClose={() => {
              setPgCompatibilityModalOpen(false);
              invalidateUniverseQueries();
            }}
            universeData={legacyUniverse}
          />
          <EnableYSQLModal
            open={ysqlModalOpen}
            onClose={() => {
              setYsqlModalOpen(false);
              invalidateUniverseQueries();
            }}
            universeData={legacyUniverse}
            enforceAuth={isAuthEnforced}
          />
          <EnableYCQLModal
            open={ycqlModalOpen}
            onClose={() => {
              setYcqlModalOpen(false);
              invalidateUniverseQueries();
            }}
            universeData={legacyUniverse}
            enforceAuth={isAuthEnforced}
            isItKubernetesUniverse={isItKubernetesUniverse}
          />
          <EditGflagsModal
            open={gflagsModalOpen}
            onClose={() => {
              setGflagsModalOpen(false);
              invalidateUniverseQueries();
            }}
            universeData={legacyUniverse}
            isGFlagMultilineConfEnabled={isGFlagMultilineConfEnabled}
            isNonRestartGFlagUpgradeOptionEnabled={false}
          />
        </>
      )}
    </Box>
  );
};
