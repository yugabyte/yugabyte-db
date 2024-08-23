import { useEffect, useState } from 'react';
import clsx from 'clsx';
import { YBLoading } from '../../common/indicators';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { isRbacEnabled } from '../../../redesign/features/rbac/common/RbacUtils';
import { LDAPAuth } from './LDAPAuth';
import { OIDCAuth } from './OIDCAuth';

const TABS = [
  {
    label: 'OIDC Configuration',
    id: 'OIDC',
    Component: OIDCAuth
  },
  {
    label: 'LDAP Configuration',
    id: 'LDAP',
    Component: LDAPAuth
  }
];

export const UserAuth = (props) => {
  const { fetchRunTimeConfigs, runtimeConfigs, isAdmin, featureFlags } = props;
  const [activeTab, setTab] = useState(TABS[0].id);

  const handleTabSelect = (e, tab) => {
    e.stopPropagation();
    setTab(tab);
  };

  const isOIDCEnabled = featureFlags.test.enableOIDC || featureFlags.released.enableOIDC;
  const isTabsVisible = isOIDCEnabled && TABS.length > 1; //Now we have only LDAP, OIDC is coming soon
  const currentTab = TABS.find((tab) => tab.id === activeTab);
  const isLoading =
    !runtimeConfigs ||
    getPromiseState(runtimeConfigs).isLoading() ||
    getPromiseState(runtimeConfigs).isInit();
  const Component = currentTab.Component;

  useEffect(() => {
    if (!isAdmin && !isRbacEnabled()) window.location.href = '/';
    else fetchRunTimeConfigs();
  }, [fetchRunTimeConfigs, isAdmin]);

  return (
    <div className="user-auth-container">
      {isTabsVisible && (
        <>
          <div className="ua-tab-container">
            {TABS.map(({ label, id }) => {
              return <div
                key={id}
                className={clsx('ua-tab-item', id === activeTab && 'ua-active-tab')}
                onClick={(e) => handleTabSelect(e, id)}
              >
                {label}
              </div>;
            })}
          </div>
          <div className="ua-sec-divider" />
        </>
      )}

      <div className={clsx('ua-form-container', !isTabsVisible && 'pl-15')}>
        {isLoading ? <YBLoading /> : <Component {...props} isTabsVisible={isTabsVisible} />}
      </div>
    </div>
  );
};
