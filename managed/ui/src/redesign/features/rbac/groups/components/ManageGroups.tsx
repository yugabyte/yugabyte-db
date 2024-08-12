/*
 * Created on Mon Jul 08 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useMethods } from 'react-use';
import { Box, makeStyles } from '@material-ui/core';
import { GroupViewContext, Pages, groupMethods, initialGroupContextState } from './GroupContext';
import ListGroups from './ListGroups';
import { CreateEditGroup } from './CreateEditGroup';

const useStyles = makeStyles((theme) => ({
  rbacContainer: {
    '& .yb-loader-circle': {
      margin: 'auto'
    }
  }
}));

const ManageGroups = () => {
  const groupContextData = useMethods(groupMethods, initialGroupContextState);

  const [
    {
      formProps: { currentPage }
    }
  ] = groupContextData;

  const classes = useStyles();

  const getCurrentView = () => {
    switch (currentPage) {
      case Pages.CREATE_GROUP:
        return <CreateEditGroup />;
      //   case 'EDIT_ROLE':
      // return <EditRole />;
      case Pages.LIST_GROUP:
        return <ListGroups />;
      default:
        return <ListGroups />;
    }
  };

  return (
    <GroupViewContext.Provider value={([...groupContextData] as unknown) as GroupViewContext}>
      <Box className={classes.rbacContainer}>{getCurrentView()}</Box>
    </GroupViewContext.Provider>
  );
};

export default ManageGroups;
