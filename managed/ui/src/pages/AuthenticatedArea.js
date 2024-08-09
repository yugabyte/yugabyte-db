// Copyright (c) YugaByte, Inc.

import { useState } from 'react';
import { find } from 'lodash';
import Cookies from 'js-cookie';
import { mouseTrap } from 'react-mousetrap';
import { useQuery } from 'react-query';
import { browserHistory } from 'react-router';

import NavBarContainer from '../components/common/nav_bar/NavBarContainer';
import AuthenticatedComponentContainer from '../components/Authenticated/AuthenticatedComponentContainer';
import { StandbyInstanceOverlay } from '../components/ha';
import { YBLoading } from '../components/common/indicators';
import { BindShortCutKeys } from './BindShortcutKeys';
import { YBIntroDialog } from './YBIntroDialog';
import { Footer } from '../components/common/footer/Footer';
import {
  RBAC_RUNTIME_FLAG,
  isRbacEnabled,
  setIsRbacEnabled
} from '../redesign/features/rbac/common/RbacUtils';
import {
  fetchUserPermissions,
  getAllAvailablePermissions,
  getApiRoutePermMapList,
  getRBACEnabledStatus,
  getRoleBindingsForUser
} from '../redesign/features/rbac/api';
import { Resource } from '../redesign/features/rbac';

const RBACAuthenticatedArea = (props) => {
  const userId = Cookies.get('userId') ?? localStorage.getItem('userId');

  const rbacEnabled = isRbacEnabled();

  const [isFeatureFlagLoaded, setIsFeatureFlagLoaded] = useState(false);

  const { isLoading: isRbacStatusLoading } = useQuery(
    ['rbac_status'],
    () => getRBACEnabledStatus(),
    {
      onSuccess: (resp) => {
        const rbac_flag = resp.data.find((flag) => flag.key === RBAC_RUNTIME_FLAG);

        if (rbac_flag) {
          setIsRbacEnabled(rbac_flag.value === 'true');
        } else {
          setIsRbacEnabled(false);
        }
      },
      onError: () => {
        setIsRbacEnabled(false);
      },
      onSettled: () => {
        setIsFeatureFlagLoaded(true);
      }
    }
  );

  const { isLoading: apiPermMapLoading } = useQuery(
    'apiRoutePermMap',
    () => getApiRoutePermMapList(),
    {
      select: (data) => data.data,
      onSuccess: (data) => {
        window.api_perm_map = data;
      },
      enabled: rbacEnabled
    }
  );

  const { isLoading: isPermissionsListLoading } = useQuery(
    'permissions',
    () => getAllAvailablePermissions(),
    {
      select: (data) => data.data,
      onSuccess: (data) => {
        window.all_permissions = data;
      },
      enabled: rbacEnabled
    }
  );

  const { isLoading } = useQuery(['user_permissions', userId], () => fetchUserPermissions(), {
    select: (data) => data.data,
    onSuccess: (data) => {
      window.rbac_permissions = data;

      if (
        find(data, { resourceType: Resource.UNIVERSE }) === undefined &&
        find(data, { resourceType: Resource.DEFAULT }) === undefined
      ) {
        browserHistory.push('/profile');
      }
    },
    onError: () => {
      browserHistory.push('/login');
    },
    enabled: rbacEnabled
  });

  const { isLoading: userRoleBindingsLoading } = useQuery(
    ['user_role_bindings', userId],
    () => getRoleBindingsForUser(userId),
    {
      select: (data) => data.data,
      onSuccess: (data) => {
        const currentUserBindings = data[userId];
        if (!currentUserBindings) return;
        window.user_role_bindings = currentUserBindings;
      },
      enabled: rbacEnabled
    }
  );

  if (
    isLoading ||
    isPermissionsListLoading ||
    isRbacStatusLoading ||
    apiPermMapLoading ||
    !isFeatureFlagLoaded ||
    userRoleBindingsLoading
  )
    return <YBLoading />;

  return (
    <AuthenticatedComponentContainer>
      <BindShortCutKeys {...props} />
      <YBIntroDialog />
      <StandbyInstanceOverlay />
      <NavBarContainer />
      <div className="container-body">{props.children}</div>
      <Footer />
    </AuthenticatedComponentContainer>
  );
};

export default mouseTrap(RBACAuthenticatedArea);
