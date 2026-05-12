import { useState } from 'react';
import { mui, YBButton, YBDropdown } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { useForm } from 'react-hook-form';
import { useQuery, useQueryClient } from 'react-query';
import { yupResolver } from '@hookform/resolvers/yup';
import {
  StyledContent,
  StyledHeader,
  StyledInfoRow,
  StyledPanel
} from '../../create-universe/components/DefaultComponents';

import { KeyboardArrowDown } from '@material-ui/icons';
import {
  convertGFlagApiRespToFormValues,
  getClusterByType,
  useEditUniverseContext
} from '../EditUniverseUtils';

import { EditGflagsModal } from '@app/redesign/features/universe/universe-actions/edit-gflags/EditGflags';
import { EditConnectionPoolModal } from '@app/redesign/features/universe/universe-actions/edit-connection-pool/EditConnectionPoolModal';
import { EditPGCompatibilityModal } from '@app/redesign/features/universe/universe-actions/edit-pg-compatibility/EditPGCompatibilityModal';
import { EnableYSQLModal } from '@app/redesign/features/universe/universe-actions/edit-ysql-ycql/EnableYSQLModal';
import { EnableYCQLModal } from '@app/redesign/features/universe/universe-actions/edit-ysql-ycql/EnableYCQLModal';
import { api, QUERY_KEY } from '@app/redesign/utils/api';
import {
  api as universeFormApi,
  QUERY_KEY as universeFormQueryKey
} from '@app/redesign/features/universe/universe-form/utils/api';
import {
  ClusterType,
  RunTimeConfigEntry
} from '@app/redesign/features/universe/universe-form/utils/dto';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import { getGetUniverseQueryKey } from '@app/v2/api/universe/universe';
import { CloudType } from '@app/redesign/helpers/dtos';

import {
  ClusterGFlagsAllOfGflagGroupsItem,
  ClusterSpecClusterType
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { ReadOnlyGflagTable } from '@app/redesign/features/universe/universe-form/form/sections/gflags/ReadOnlyGflagsModal';
import { GFlagsFieldNew } from '@app/redesign/features/universe/universe-form/form/fields/GflagsField/GflagsFieldNew';
import { DatabaseSettingsProps } from '../../create-universe/steps/database-settings/dtos';
import { DatabaseValidationSchema } from '../../create-universe/steps/database-settings/ValidationSchema';

import Checked from '@app/redesign/assets/check-new.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import Disabled from '@app/redesign/assets/revoke.svg';
import AddCircleIcon from '@app/redesign/assets/add-circle-blue.svg';
import {
  getClusterByType as getLegacyClusterType,
  transformSpecificGFlagToFlagsArray,
  transformGFlagToFlagsArray
} from '@app/redesign/features/universe/universe-form/utils/helpers';

const { Box, Grid2: Grid, MenuItem, styled } = mui;

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
  const queryClient = useQueryClient();
  const { universeData } = useEditUniverseContext();

  const [connectionPoolModalOpen, setConnectionPoolModalOpen] = useState(false);
  const [pgCompatibilityModalOpen, setPgCompatibilityModalOpen] = useState(false);
  const [ysqlModalOpen, setYsqlModalOpen] = useState(false);
  const [ycqlModalOpen, setYcqlModalOpen] = useState(false);
  const [gflagsModalOpen, setGflagsModalOpen] = useState(false);

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

  const legacyPrimaryCluster = legacyUniverse?.universeDetails
    ? getLegacyClusterType(legacyUniverse.universeDetails, ClusterType.PRIMARY)
    : undefined;
  const userIntent = legacyPrimaryCluster?.userIntent;
  const legacyGflags = userIntent?.specificGFlags
    ? transformSpecificGFlagToFlagsArray(userIntent.specificGFlags)
    : transformGFlagToFlagsArray(userIntent?.masterGFlags, userIntent?.tserverGFlags);

  return (
    <Box sx={{ display: 'flex', flexDirection: 'column', gap: 3 }}>
      <StyledPanel>
        <StyledHeader>
          <Grid alignContent={'center'} justifyContent={'space-between'} container>
            {t('interface')}
            <YBDropdown
              sx={{ width: '340px' }}
              dataTestId="edit-ysql-ycql-actions"
              origin={
                <YBButton
                  variant="secondary"
                  dataTestId="edit-ysql-ycql-actions-button"
                  endIcon={<KeyboardArrowDown />}
                >
                  {t('actions', { keyPrefix: 'common' })}
                </YBButton>
              }
            >
              <MenuItem
                data-test-id="edit-ysql-settings"
                data-testid="edit-ysql-settings"
                disabled={isLegacyUniverseLoading || !legacyUniverse}
                onClick={() => setYsqlModalOpen(true)}
              >
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editYSQLSettings')}
              </MenuItem>
              <MenuItem
                data-test-id="edit-ycql-settings"
                data-testid="edit-ycql-settings"
                disabled={isLegacyUniverseLoading || !legacyUniverse}
                onClick={() => setYcqlModalOpen(true)}
              >
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editYCQLSettings')}
              </MenuItem>
            </YBDropdown>
          </Grid>
        </StyledHeader>
        <StyledContent sx={{ gap: '24px' }}>
          <StyledInfoRow sx={{ flexDirection: 'row', gap: '190px' }}>
            <div>
              <span className="header">{t('ysql')}</span>
              <span className="value sameline nogap">
                {t(ysqlEnabed ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {ysqlEnabed ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
            <div>
              <span className="header">{t('ysqlAuthentication')}</span>{' '}
              <span className="value sameline nogap">
                {t(ysqlAuthEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {ysqlAuthEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
          </StyledInfoRow>
          <StyledInfoRow sx={{ flexDirection: 'row', gap: '190px' }}>
            <div>
              <span className="header">{t('ycql')}</span>
              <span className="value sameline nogap">
                {t(ycqlEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {ycqlEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
            <div>
              <span className="header">{t('ycqlAuthentication')}</span>{' '}
              <span className="value sameline nogap">
                {t(ycqlAuthEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {ycqlAuthEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
          </StyledInfoRow>
        </StyledContent>
      </StyledPanel>
      <StyledPanel>
        <StyledHeader>
          <Grid alignContent={'center'} justifyContent={'space-between'} container>
            {t('features')}
            <YBDropdown
              sx={{ width: '340px' }}
              dataTestId="edit-features-actions"
              origin={
                <YBButton
                  variant="secondary"
                  dataTestId="edit-features-actions-button"
                  endIcon={<KeyboardArrowDown />}
                >
                  {t('actions', { keyPrefix: 'common' })}
                </YBButton>
              }
            >
              <MenuItem
                data-test-id="edit-pooling-settings"
                data-testid="edit-pooling-settings"
                disabled={isLegacyUniverseLoading || !legacyUniverse}
                onClick={() => setConnectionPoolModalOpen(true)}
              >
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editConnectionPooling')}
              </MenuItem>
              <MenuItem
                data-test-id="edit-postgres-compatibility-settings"
                data-testid="edit-postgres-compatibility-settings"
                disabled={isLegacyUniverseLoading || !legacyUniverse}
                onClick={() => setPgCompatibilityModalOpen(true)}
              >
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editPostgresCompatibility')}
              </MenuItem>
            </YBDropdown>
          </Grid>
        </StyledHeader>
        <StyledContent sx={{ gap: '24px' }}>
          <StyledInfoRow>
            <div>
              <span className="header">{t('connectionPooling')}</span>
              <span className="value sameline nogap">
                {t(connectionPoolingEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {connectionPoolingEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
          </StyledInfoRow>
          <StyledInfoRow>
            <div>
              <span className="header">{t('postgresCompatibility')}</span>
              <span className="value sameline nogap">
                {t(postgresCompatibilityEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                {postgresCompatibilityEnabled ? <CheckedIcon /> : <DisabledIcon />}
              </span>
            </div>
          </StyledInfoRow>
        </StyledContent>
      </StyledPanel>
      <StyledPanel>
        <StyledHeader
          sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
        >
          {t('advancedConfigFlags')}
          {legacyGflags.length > 0 && (
            <YBButton
              dataTestId="edit-gflags-button"
              variant="ghost"
              startIcon={<EditIcon />}
              disabled={isLegacyUniverseLoading || !legacyUniverse}
              onClick={() => setGflagsModalOpen(true)}
            >
              {t('edit', { keyPrefix: 'common' })}
            </YBButton>
          )}
        </StyledHeader>
        <StyledContent>
          {legacyGflags.length <= 0 ? (
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
              <YBButton
                variant="secondary"
                dataTestId="add-gflags-button"
                startIcon={<AddCircleIcon />}
                sx={{ mt: 2 }}
                disabled={isLegacyUniverseLoading || !legacyUniverse}
                onClick={() => setGflagsModalOpen(true)}
              >
                {t('addFlag')}
              </YBButton>
            </Box>
          ) : (
            <ReadOnlyGflagTable gFlags={legacyGflags} isPrimary={true} />
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
          />
        </>
      )}
    </Box>
  );
};
