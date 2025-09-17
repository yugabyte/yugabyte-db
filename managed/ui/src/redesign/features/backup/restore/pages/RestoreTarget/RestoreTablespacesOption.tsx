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
import { GetRestoreContext, getUnSupportedTableSpaceConfig } from '../../RestoreUtils';
import { AlertVariant, YBAlert, YBCheckbox } from '../../../../../components';

import { RestoreFormModel } from '../../models/RestoreFormModel';
import { TablespaceUnSupported } from './TablespaceUnSupported';

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
  unSupportedTablespace: {
    marginLeft: '34px',
    marginTop: '8px'
  },
  link: {
    color: theme.palette.grey[700],
    textDecoration: 'underline',
    cursor: 'pointer',
    '&:hover, &:focus': {
      color: theme.palette.grey[700],
      textDecoration: 'underline'
    }
  }
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

  const hasUnsupportedTablespaces = getUnSupportedTableSpaceConfig(
    preflightResponse,
    'unsupportedTablespaces'
  );
  const hasConflictingTablespaces = getUnSupportedTableSpaceConfig(
    preflightResponse,
    'conflictingTablespaces'
  );

  return (
    <div>
      <YBCheckbox
        label={t('restoreTablespaces')}
        name="target.useTablespaces"
        checked={useTablespaces}
        onChange={(event) => {
          setValue('target.useTablespaces', event.target.checked);
        }}
        disabled={hasUnsupportedTablespaces}
      />
      {!hasUnsupportedTablespaces && (
        <div className={classes.helperText}>
          <Trans i18nKey="restoreTablespacesHelpText" components={{ b: <b /> }} t={t} />
        </div>
      )}
      {hasUnsupportedTablespaces && (
        <div className={classes.unSupportedTablespace}>
          <TablespaceUnSupported loggingID={preflightResponse.loggingID} />
        </div>
      )}
      {useTablespaces && !hasUnsupportedTablespaces && hasConflictingTablespaces && (
        <YBAlert
          text={
            <Trans
              i18nKey="tablespaceConflictResolution"
              t={t}
              components={{
                a: (
                  <a
                    className={classes.link}
                    href={`/logs?queryRegex=${preflightResponse.loggingID}`}
                    target="_blank"
                    rel="noreferrer"
                  />
                ),
                b: <b />
              }}
            />
          }
          open
          variant={AlertVariant.Warning}
          className={classes.conflictingTablespace}
        />
      )}
    </div>
  );
};

export default RestoreTablespacesOption;
