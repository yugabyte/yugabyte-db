import { useTranslation } from 'react-i18next';
import { mui, YBButton, YBTag } from '@yugabyte-ui-library/core';
import { useToggle } from 'react-use';
import {
  StyledContent,
  StyledHeader,
  StyledInfoRow,
  StyledPanel
} from '../../create-universe/components/DefaultComponents';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';

import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { EditAdvancedSettingsModal } from '../edit-advanced/EditAdvancedSettingsModal';

import Checked from '@app/redesign/assets/check-new.svg';
import EditIcon from '@app/redesign/assets/edit2.svg';
import Disabled from '@app/redesign/assets/revoke.svg';

const { styled } = mui;

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

export const AdvancedTab = () => {
  const { universeData } = useEditUniverseContext();
  const { t } = useTranslation('translation', {
    keyPrefix: 'editUniverse.advanced'
  });
  const [isEditAdvancedSettingsModalVisible, setEditAdvancedSettingsModalVisible] = useToggle(
    false
  );
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const networking_spec = primaryCluster?.networking_spec;

  const getNoProxyList = () => {
    if (!networking_spec?.proxy_config?.no_proxy_list?.length) {
      return '-';
    }
    return (
      <>
        {networking_spec?.proxy_config?.no_proxy_list?.[0]}
        <YBTag size="small" variant="light">
          +{(networking_spec?.proxy_config?.no_proxy_list?.length ?? 1) - 1}
        </YBTag>
      </>
    );
  };
  return (
    <StyledPanel>
      <StyledHeader sx={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        {t('proxyConfiguration')}
        <YBButton
          dataTestId="edit-security-transit-button"
          variant="ghost"
          startIcon={<EditIcon />}
          onClick={() => {
            setEditAdvancedSettingsModalVisible(true);
          }}
        >
          {t('edit', { keyPrefix: 'common' })}
        </YBButton>
      </StyledHeader>
      <StyledContent>
        <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
          <div>
            <span className="header">{t('proxyServer')}</span>
            <span className="value sameline nogap">
              {t(networking_spec?.proxy_config ? 'enabled' : 'disabled', { keyPrefix: 'common' })}
              {networking_spec?.proxy_config ? <CheckedIcon /> : <DisabledIcon />}
            </span>
          </div>
        </StyledInfoRow>
        <StyledInfoRow sx={{ flexDirection: 'row', gap: '90px' }}>
          <div style={{ width: '300px' }}>
            <span className="header">{t('secureWebProxy')}</span>
            <span className="value ">{networking_spec?.proxy_config?.https_proxy ?? '-'}</span>
          </div>
          <div style={{ width: '300px' }}>
            <span className="header">{t('webProxy')}</span>
            <span className="value ">{networking_spec?.proxy_config?.http_proxy ?? '-'}</span>
          </div>
          <div style={{ width: '300px' }}>
            <span className="header">{t('bypassProxyList')}</span>
            <span className="value sameline">{getNoProxyList()}</span>
          </div>
        </StyledInfoRow>
      </StyledContent>
      <EditAdvancedSettingsModal
        visible={isEditAdvancedSettingsModalVisible}
        onClose={() => setEditAdvancedSettingsModalVisible(false)}
      />
    </StyledPanel>
  );
};
