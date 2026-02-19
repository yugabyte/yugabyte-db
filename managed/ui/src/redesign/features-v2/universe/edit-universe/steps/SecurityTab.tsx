import { useTranslation } from 'react-i18next';
import { mui, YBButton } from '@yugabyte-ui-library/core';
import {
  StyledContent,
  StyledHeader,
  StyledInfoRow,
  StyledPanel
} from '../../create-universe/components/DefaultComponents';
import { FormProvider, useForm } from 'react-hook-form';
import { SecuritySettingsProps } from '../../create-universe/steps/security-settings/dtos';
import { AssignPublicIPField } from '../../create-universe/fields';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

import Checked from '@app/redesign/assets/check-new.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import Disabled from '@app/redesign/assets/revoke.svg';

const { styled, Box } = mui;

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
  const methods = useForm<SecuritySettingsProps>();
  const { universeData } = useEditUniverseContext();
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);

  const providerCode = primaryCluster?.placement_spec?.cloud_list[0].code;
  const nodeToNodeEnabled = !!universeData?.spec?.encryption_in_transit_spec
    ?.enable_node_to_node_encrypt;
  const clientToNodeEnabled = !!universeData?.spec?.encryption_in_transit_spec
    ?.enable_client_to_node_encrypt;
  const encryptionAtRestEnabled = !!universeData?.spec?.encryption_at_rest_spec?.kms_config_uuid;
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
              onClick={() => {}}
              disabled
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
              onClick={() => {}}
              disabled
            >
              {t('edit', { keyPrefix: 'common' })}
            </YBButton>
          </StyledHeader>
          <StyledContent>
            <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
              <div>
                <span className="header">{t('encryption')}</span>
                <span className="value sameline nogap">
                  {t(encryptionAtRestEnabled ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
                  {encryptionAtRestEnabled ? <CheckedIcon /> : <DisabledIcon />}
                </span>
              </div>
            </StyledInfoRow>
          </StyledContent>
        </StyledPanel>
      </Box>
    </FormProvider>
  );
};
