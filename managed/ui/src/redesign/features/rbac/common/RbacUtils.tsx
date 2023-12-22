/*
 * Created on Wed Aug 09 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import clsx from 'clsx';
import { makeStyles } from '@material-ui/core';
import { Role } from '../roles';

const useStyles = makeStyles((theme) => ({
  roleType: {
    borderRadius: theme.spacing(0.5),
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    padding: '2px 6px',
    '&.custom': {
      border: `1px solid ${theme.palette.primary[300]}`,
      background: theme.palette.primary[200],
      color: theme.palette.primary[600]
    }
  }
}));

export const RoleTypeComp: FC<{ role: Role }> = ({ role }) => {
  const classes = useStyles();
  return (
    <span className={clsx(classes.roleType, role.roleType === 'Custom' && 'custom')}>
      {role.roleType}
    </span>
  );
};
