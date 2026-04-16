import { mui, YBButton, YBDropdown } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';
import { useForm } from 'react-hook-form';
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

import {
  ClusterGFlagsAllOfGflagGroupsItem,
  ClusterSpecClusterType
} from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { GFlagsFieldNew } from '@app/redesign/features/universe/universe-form/form/fields/GflagsField/GflagsFieldNew';
import { DatabaseSettingsProps } from '../../create-universe/steps/database-settings/dtos';
import { DatabaseValidationSchema } from '../../create-universe/steps/database-settings/ValidationSchema';

import Checked from '@app/redesign/assets/check-new.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import Disabled from '@app/redesign/assets/revoke.svg';

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
  const { universeData } = useEditUniverseContext();

  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);

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
              <MenuItem data-test-id="edit-ysql-settings">
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editYSQLSettings')}
              </MenuItem>
              <MenuItem data-test-id="edit-ycql-settings">
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
              <MenuItem data-test-id="edit-pooling-settings">
                <Box
                  sx={{ display: 'flex', alignItems: 'center', flexDirection: 'row', gap: '4px' }}
                >
                  <EditIcon />
                </Box>
                {t('editConnectionPooling')}
              </MenuItem>
              <MenuItem data-test-id="edit-postgres-compatibility-settings">
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
        <StyledHeader>{t('advancedConfigFlags')}</StyledHeader>
        <StyledContent>
          <GFlagsFieldNew
            control={control}
            fieldPath={'gFlags'}
            dbVersion={databaseVersion ?? ''}
            isReadReplica={false}
            editMode={false}
            isGFlagMultilineConfEnabled={false}
            isPGSupported={true}
            isReadOnly={false}
          />
        </StyledContent>
      </StyledPanel>
    </Box>
  );
};
