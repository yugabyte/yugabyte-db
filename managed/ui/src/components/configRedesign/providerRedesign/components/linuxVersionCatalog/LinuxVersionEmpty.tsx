/*
 * Created on Mon Nov 20 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Typography, makeStyles } from '@material-ui/core';
import { YBButton } from '../../../../../redesign/components';
import { RbacValidator } from '../../../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../../redesign/features/rbac/ApiAndUserPermMapping';
import { Add } from '@material-ui/icons';

const useStyles = makeStyles((theme) => ({
  root: {
    borderRadius: '4px',
    border: `1px solid #E3E3E5`,
    background: `#FAFAFB`,
    display: 'flex',
    flexDirection: 'column',
    gap: '16px',
    alignItems: 'center',
    padding: '50px 0px'
  },
  info: {
    color: '#545454'
  }
}));

interface LinuxVersionEmptyProps {
  onAdd: () => void;
  viewMode: 'CREATE' | 'EDIT';
}

export const LinuxVersionEmpty: FC<LinuxVersionEmptyProps> = ({ onAdd, viewMode }) => {
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'linuxVersion'
  });

  return (
    <div className={classes.root}>
      <Typography variant="subtitle1">{t('emptyCard.info')}</Typography>
      <RbacValidator
        accessRequiredOn={
          viewMode === 'CREATE'
            ? ApiPermissionMap.CREATE_PROVIDER
            : ApiPermissionMap.MODIFY_PROVIDER
        }
        isControl
      >
        <YBButton variant="secondary" startIcon={<Add />} onClick={() => onAdd()}>
          {t('addLinuxVersion')}
        </YBButton>
      </RbacValidator>
    </div>
  );
};
