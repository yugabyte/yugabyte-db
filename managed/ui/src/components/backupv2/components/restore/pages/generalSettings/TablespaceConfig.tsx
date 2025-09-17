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
import { AlertVariant, YBAlert, YBCheckbox } from '../../../../../../redesign/components';
import { Box, makeStyles } from '@material-ui/core';
import { IGeneralSettings } from './GeneralSettings';
import { RestoreContextMethods, RestoreFormContext } from '../../RestoreContext';
import { getUnSupportedTableSpaceConfig } from '@app/redesign/features/backup/restore/RestoreUtils';
import { TablespaceUnSupported } from '@app/redesign/features/backup/restore/pages/RestoreTarget/TablespaceUnSupported';
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
    marginTop: '8px',
    '& > div': {
      alignSelf: 'flex-start'
    },
    '& svg': {
      marginTop: '0px'
    }
  },
  unSupportedTablespace: {
    marginLeft: '34px',
    marginTop: '5px'
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

  const hasUnsupportedTablespaces = getUnSupportedTableSpaceConfig(
    preflightResponse,
    'unsupportedTablespaces'
  );
  const hasConflictingTablespaces = getUnSupportedTableSpaceConfig(
    preflightResponse,
    'conflictingTablespaces'
  );

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
        disabled={hasUnsupportedTablespaces}
      />
      {!hasUnsupportedTablespaces && (
        <span className={classes.tablespaceHelpText}>
          <Trans i18nKey="newRestoreModal.tablespaces.checkboxHelpText" components={{ b: <b /> }} />
        </span>
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
              i18nKey={`${TRANS_PREFIX}.tablespaceConflictResolution`}
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
    </Box>
  );
};
