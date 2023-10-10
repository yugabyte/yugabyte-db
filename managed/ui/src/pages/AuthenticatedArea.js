// Copyright (c) YugaByte, Inc.

import Cookies from 'js-cookie';
import { mouseTrap } from 'react-mousetrap';
import { useQuery } from 'react-query';
import { useMount } from 'react-use';
import { browserHistory } from 'react-router';
import { useSelector } from 'react-redux';

import NavBarContainer from '../components/common/nav_bar/NavBarContainer';
import AuthenticatedComponentContainer from '../components/Authenticated/AuthenticatedComponentContainer';
import { Footer } from '../components/common/footer';
import { StandbyInstanceOverlay } from '../components/ha';
import { YBLoading } from '../components/common/indicators';
import { BindShortCutKeys } from './BindShortcutKeys';
import { YBIntroDialog } from './YBIntroDialog';


import { isRbacEnabled } from '../redesign/features/rbac/common/RbacUtils';
import { fetchUserPermissions, getAllAvailablePermissions } from '../redesign/features/rbac/api';
import { hasNecessaryPerm } from '../redesign/features/rbac/common/RbacValidator';
import { Action, Resource } from '../redesign/features/rbac';


const RBACAuthenticatedArea = (props) => {

  const userId = Cookies.get('userId') ?? localStorage.getItem('userId');

  const rbacEnabled = isRbacEnabled();

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

  if (isLoading || isPermissionsListLoading) return <YBLoading />;

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
