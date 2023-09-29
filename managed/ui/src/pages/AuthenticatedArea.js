// Copyright (c) YugaByte, Inc.

import Cookies from 'js-cookie';
import { mouseTrap } from 'react-mousetrap';
import { useQuery } from 'react-query';

import NavBarContainer from '../components/common/nav_bar/NavBarContainer';
import AuthenticatedComponentContainer from '../components/Authenticated/AuthenticatedComponentContainer';
import { Footer } from '../components/common/footer';
import { StandbyInstanceOverlay } from '../components/ha';
import { YBLoading } from '../components/common/indicators';
import { BindShortCutKeys } from './BindShortcutKeys';
import { YBIntroDialog } from './YBIntroDialog';


import { fetchUserPermissions } from '../redesign/features/rbac/api';
import { useSelector } from 'react-redux';

const RBACAuthenticatedArea = (props) => {

  const userId = Cookies.get('userId') ?? localStorage.getItem('userId');
  const featureFlags = useSelector((state) => state.featureFlags);

  const { isLoading } = useQuery(['user_permissions', userId], () => fetchUserPermissions(), {
    select: data => data.data,
    onSuccess: (data) => {
      window.rbac_permissions = data;
    },
    enabled: featureFlags.test.enableRBAC
  });

  if (isLoading) return <YBLoading />;

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
