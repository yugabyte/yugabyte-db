// Copyright (c) YugaByte, Inc.

import { useState } from 'react';
import Cookies from 'js-cookie';
import { mouseTrap } from 'react-mousetrap';
import { useQuery } from 'react-query';
import { browserHistory } from 'react-router';

import NavBarContainer from '../components/common/nav_bar/NavBarContainer';
import AuthenticatedComponentContainer from '../components/Authenticated/AuthenticatedComponentContainer';
import { Footer } from '../components/common/footer';
import { StandbyInstanceOverlay } from '../components/ha';
import { YBLoading } from '../components/common/indicators';
import { BindShortCutKeys } from './BindShortcutKeys';
import { YBIntroDialog } from './YBIntroDialog';


import { RBAC_RUNTIME_FLAG, isRbacEnabled, setIsRbacEnabled } from '../redesign/features/rbac/common/RbacUtils';
import { api } from '../redesign/features/universe/universe-form/utils/api';
import { fetchUserPermissions, getAllAvailablePermissions } from '../redesign/features/rbac/api';
import { hasNecessaryPerm } from '../redesign/features/rbac/common/RbacValidator';
import { Action, Resource } from '../redesign/features/rbac';


const RBACAuthenticatedArea = (props) => {

  const userId = Cookies.get('userId') ?? localStorage.getItem('userId');


  const [runtimeConfigLoaded, setRuntimeConfigLoaded] = useState(false);

  const rbacEnabled = isRbacEnabled();

  const { isLoading: isRuntimConfigLoading } = useQuery(['runtime_configs'], () => api.fetchRunTimeConfigs(), {
    onSuccess: (res) => {
      const rbacKey = res.configEntries?.find(
        (c) => c.key === RBAC_RUNTIME_FLAG
      );
      setIsRbacEnabled(rbacKey?.value === 'true');
      setRuntimeConfigLoaded(true);
    },
    onError: (resp) => {
      setIsRbacEnabled(resp?.response?.status === 401);
      setRuntimeConfigLoaded(true);
    }
  });

  const { isLoading: isPermissionsListLoading } = useQuery('permissions', () => getAllAvailablePermissions(), {
    select: data => data.data,
    onSuccess: (data) => {
      window.all_permissions = data;
    },
    enabled: rbacEnabled
  });


  const { isLoading } = useQuery(['user_permissions', userId], () => fetchUserPermissions(), {
    select: data => data.data,
    onSuccess: (data) => {
      window.rbac_permissions = data;

      if (!hasNecessaryPerm({ //if the user is connect only user, then redirect him to profile page
        permissionRequired: [Action.READ],
        resourceType: Resource.DEFAULT
      })) {
        browserHistory.push('/profile');
      };
    },
    onError: () => {
      browserHistory.push('/login');
    },
    enabled: rbacEnabled
  });

  if (isLoading || isPermissionsListLoading || isRuntimConfigLoading || !runtimeConfigLoaded) return <YBLoading />;

  return (
    <AuthenticatedComponentContainer>
      <BindShortCutKeys {...props} />
      <YBIntroDialog />
      <StandbyInstanceOverlay />
      <NavBarContainer />
      <div className="container-body">
        {props.children}
      </div>
      <Footer />

    </AuthenticatedComponentContainer>
  );
};

export default mouseTrap(RBACAuthenticatedArea);
