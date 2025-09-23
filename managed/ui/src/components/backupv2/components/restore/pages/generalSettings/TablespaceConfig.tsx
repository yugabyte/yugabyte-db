/*
 * Created on Wed Aug 23 2023
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext } from 'react';
import { keys } from 'lodash';
import { useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { YBCheckbox } from '../../../../../../redesign/components';
import { Box, makeStyles } from '@material-ui/core';
import { IGeneralSettings } from './GeneralSettings';
import { TablespaceWarnings } from '@app/redesign/features/backup/restore/pages/RestoreTarget/TablespaceWarnings';
import { RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import UnChecked from '../../../../../../redesign/assets/checkbox/UnChecked.svg';
import Checked from '../../../../../../redesign/assets/checkbox/Checked.svg';

const TRANS_PREFIX = 'backup.restore.target';

const useStyles = makeStyles((theme) => ({
  warning: {
    color: theme.palette.warning[900]
  },
  root: {
    width: '650px',
    borderRadius: theme.spacing(1),
    border: `1px solid ${theme.palette.ybacolors.backgroundGrayDark}`,
    background: theme.palette.common.white,
    padding: '10px'
  },
  tablespaceHelpText: {
    color: '#67666C',
    fontSize: '12px',
    display: 'inline-block',
    marginLeft: theme.spacing(4.25)
  },
  conflictingTablespace: {
    marginLeft: '34px',
    marginTop: '5px'
  },
}));

export const TablespaceConfig = () => {
  const [
    {
      formData: { preflightResponse }
    }
  ]: RestoreContextMethods = (useContext(RestoreFormContext) as unknown) as RestoreContextMethods;
  const { t } = useTranslation();

  const { watch, setValue } = useFormContext<IGeneralSettings>();
  const useTablespaces = watch('useTablespaces');
  const classes = useStyles();

  if (preflightResponse === undefined) {
    return null;
  }

  const storageLocationsKeys = keys(preflightResponse.perLocationBackupInfoMap);

  const supportsTablespaces = storageLocationsKeys.some((location) => {
    const tablespaceResponse =
      preflightResponse.perLocationBackupInfoMap[location].tablespaceResponse;
    return tablespaceResponse.containsTablespaces;
  });

  if (!supportsTablespaces) {
    return null;
  }

  return (
    <Box className={classes.root}>
      <YBCheckbox
        label={t('newRestoreModal.tablespaces.checkboxTitle')}
        checked={useTablespaces}
        name="useTablespaces"
        icon={<img src={UnChecked} alt="unchecked" />}
        checkedIcon={<img src={Checked} alt="checked" />}
        onChange={(event) => {
          setValue('useTablespaces', event.target.checked);
        }}
      />
      <span className={classes.tablespaceHelpText}>
        <Trans i18nKey="newRestoreModal.tablespaces.checkboxHelpText" components={{ b: <b /> }} />
      </span>
      <div className={classes.conflictingTablespace}>
        <TablespaceWarnings preflightResponse={preflightResponse} />
      </div>
    </Box>
  );
};
