/*
 * Created on Mon Jul 31 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useMethods } from 'react-use';
import { Box, makeStyles } from '@material-ui/core';
import { CreateUsers } from './CreateUsers';
import { EditUser } from './EditUsers';
import { ViewUsers } from './ViewUsers';
import { UniverseResource } from '../../policy/IPolicy';
import { UserContext, UserViewContext, defaultUserContext, userMethods } from './UserContext';

const useStyles = makeStyles((theme) => ({
  rbacContainer: {
    '& .yb-loader-circle': {
      margin: 'auto'
    }
  }
}));

export const ManageUsers = () => {
  const userContextData = useMethods(userMethods, defaultUserContext);
  const [
    {
      formProps: { currentPage }
    }
  ] = userContextData;

  const classes = useStyles();

  const getCurrentView = () => {
    switch (currentPage) {
      case 'CREATE_USER':
        return <CreateUsers />;
      case 'EDIT_USER':
        return <EditUser />;
      default:
        return <ViewUsers />;
    }
  };
  return (
    <UserViewContext.Provider
      value={([...userContextData] as unknown) as UserContext<UniverseResource>}
    >
      <Box className={classes.rbacContainer}>{getCurrentView()}</Box>
    </UserViewContext.Provider>
  );
};
