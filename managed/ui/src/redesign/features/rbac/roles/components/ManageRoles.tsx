/*
 * Created on Wed Jul 12 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { CreateRoleWithContainer } from './CreateRole';
import { useMethods } from 'react-use';
import { Box } from '@material-ui/core';
import { RoleViewContext, initialRoleContextState, roleMethods } from '../RoleContext';
import ListRoles from './ListRoles';
import { EditRole } from './EditRole';

const ManageRoles = () => {
  const roleContextData = useMethods(roleMethods, initialRoleContextState);

  const [
    {
      formProps: { currentPage }
    }
  ] = roleContextData;

  const getCurrentView = () => {
    switch (currentPage) {
      case 'CREATE_ROLE':
        return <CreateRoleWithContainer />;
      case 'EDIT_ROLE':
        return <EditRole />;
      default:
        return <ListRoles />;
    }
  };

  return (
    <RoleViewContext.Provider value={([...roleContextData] as unknown) as RoleViewContext}>
      <Box className="rbac-container">{getCurrentView()}</Box>
    </RoleViewContext.Provider>
  );
};

export default ManageRoles;
