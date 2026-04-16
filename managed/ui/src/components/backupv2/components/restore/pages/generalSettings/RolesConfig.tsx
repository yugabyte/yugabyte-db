/*
 * Created on Wed Aug 23 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { YBCheckbox } from '../../../../../../redesign/components';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { IGeneralSettings } from './GeneralSettings';

import UnChecked from '../../../../../../redesign/assets/checkbox/UnChecked.svg?img';
import Checked from '../../../../../../redesign/assets/checkbox/Checked.svg?img';

const useStyles = makeStyles((theme) => ({
  warning: {
    color: theme.palette.warning[900]
  },
  root: {
    width: '650px',
    borderRadius: theme.spacing(1),
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    background: theme.palette.common.white,
    padding: '10px',
    flexDirection: 'column',
    marginTop: theme.spacing(1)
  },
  tablespaceHelpText: {
    color: '#67666C',
    fontSize: '12px',
    display: 'block',
    marginLeft: theme.spacing(4.25)
  },
  conflictingTablespace: {
    marginLeft: '34px',
    marginTop: '5px'
  }
}));

export const RolesConfig = () => {
  const { t } = useTranslation();

  const { watch, setValue } = useFormContext<IGeneralSettings>();
  const useRoles = watch('useRoles');
  const classes = useStyles();

  return (
    <Box>
      <Typography variant="body1">{t('newRestoreModal.rolesConfig.title')}</Typography>
      <Box className={classes.root}>
        <Box>
          <YBCheckbox
            label={t('newRestoreModal.rolesConfig.checkboxTitle')}
            checked={useRoles}
            name="useRoles"
            icon={<img src={UnChecked} alt="unchecked" />}
            checkedIcon={<img src={Checked} alt="checked" />}
            onChange={(event) => {
              setValue('useRoles', event.target.checked);
            }}
          />
        </Box>
      </Box>
    </Box>
  );
};
