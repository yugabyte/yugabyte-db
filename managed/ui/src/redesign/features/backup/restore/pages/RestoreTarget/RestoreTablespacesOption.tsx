/*
 * Created on Wed Aug 21 2024
 *
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { keys } from 'lodash';
import { useFormContext } from 'react-hook-form';
import { Trans, useTranslation } from 'react-i18next';
import { makeStyles } from '@material-ui/core';
import { TablespaceWarnings } from './TablespaceWarnings';
import { GetRestoreContext } from '../../RestoreUtils';
import { YBCheckbox } from '../../../../../components';

import { RestoreFormModel } from '../../models/RestoreFormModel';

const useStyles = makeStyles((theme) => ({
  helperText: {
    marginLeft: '35px',
    color: theme.palette.ybacolors.textDarkGray,
    fontSize: '12px'
  },
  conflictingTablespace: {
    marginLeft: '34px',
    marginTop: '5px'
  },
}));
const RestoreTablespacesOption: FC = () => {
  const [{ preflightResponse }] = GetRestoreContext();
  const { watch, setValue } = useFormContext<RestoreFormModel>();
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.target'
  });
  const classes = useStyles();
  const useTablespaces = watch('target.useTablespaces');

  if (preflightResponse === null) {
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
    <div>
      <YBCheckbox
        label={t('restoreTablespaces')}
        name="target.useTablespaces"
        checked={useTablespaces}
        onChange={(event) => {
          setValue('target.useTablespaces', event.target.checked);
        }}
      />
      <div className={classes.helperText}>
        <Trans i18nKey="restoreTablespacesHelpText" components={{ b: <b /> }} t={t} />
      </div>
      <div className={classes.conflictingTablespace}>
        <TablespaceWarnings preflightResponse={preflightResponse} />
      </div>
    </div>
  );
};

export default RestoreTablespacesOption;
