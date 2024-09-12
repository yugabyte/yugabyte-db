/*
 * Created on Wed Aug 21 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useFormContext } from 'react-hook-form';
import { makeStyles } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { RestoreFormModel } from '../../models/RestoreFormModel';
import { YBCheckboxField } from '../../../../../components';

const useStyles = makeStyles((theme) => ({
  renameKeyspaceHelperText: {
    marginLeft: '35px',
    color: theme.palette.ybacolors.textDarkGray,
    fontSize: '12px'
  }
}));

const RenameKeyspaceOption: FC = () => {
  const { control, watch } = useFormContext<RestoreFormModel>();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.target'
  });

  const forceKeyspaceRename = watch('target.forceKeyspaceRename');
  const classes = useStyles();

  return (
    <div>
      <YBCheckboxField
        label={t('renameKeyspaces', {
          Optional: !forceKeyspaceRename ? t('optional') : ''
        })}
        control={control}
        name="target.renameKeyspace"
        disabled={forceKeyspaceRename}
      />
      {forceKeyspaceRename && (
        <div className={classes.renameKeyspaceHelperText}>
          <Trans i18nKey="forceRenameHelpText" t={t} components={{ b: <b /> }} />
        </div>
      )}
    </div>
  );
};

export default RenameKeyspaceOption;
