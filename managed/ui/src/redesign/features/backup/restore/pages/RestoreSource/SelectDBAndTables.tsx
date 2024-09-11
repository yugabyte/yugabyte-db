/*
 * Created on Tue Aug 20 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { useContext, useEffect, useState } from 'react';
import { Control, FieldValues, useForm, useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { makeStyles, Typography } from '@material-ui/core';
import { useQuery } from 'react-query';
import { useMount } from 'react-use';

import {
  ReactSelectComponents,
  ReactSelectStyles
} from '../../../scheduled/create/ReactSelectStyles';
import { RestoreFormModel } from '../../models/RestoreFormModel';
import SelectTables from '../../../scheduled/create/pages/BackupObjects/SelectTables';
import { TableType } from '../../../../../helpers/dtos';
import { RestoreContextMethods, RestoreFormContext } from '../../models/RestoreContext';
import { getRestorableTables, RestorableTablesResp } from '../../api/api';
import { isDefinedNotNull } from '../../../../../../utils/ObjectUtils';
import {
  BACKUP_API_TYPES,
  Backup_Options_Type,
  ITable
} from '../../../../../../components/backupv2';
import { YBReactSelectField } from '../../../../../../components/configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';

import {
  doesUserSelectsSingleKeyspaceToRestore,
  isRestoreTriggeredFromIncBackup
} from '../../RestoreUtils';

const useStyles = makeStyles(() => ({
  root: {
    display: 'flex',
    gap: '16px',
    flexDirection: 'column'
  },
  keyspaceSelect: {
    width: '550px'
  },
  '@global': {
    'body>div#menu-source\\.keyspace': {
      zIndex: '99999 !important',
      '& .MuiListSubheader-sticky': {
        top: 'auto'
      }
    }
  }
}));

export const SelectDBAndTables = () => {
  const {
    setValue,
    watch,
    formState: { errors }
  } = useFormContext<RestoreFormModel>();

  const [{ backupDetails, additionalBackupProps }, { setRestorableTables }] = (useContext(
    RestoreFormContext
  ) as unknown) as RestoreContextMethods;

  // list of tables present in the selected keyspace
  const [tablesInSelectedKeyspace, setTablesInSelectedKeyspace] = useState<ITable[]>([]);

  // list of keyspaces available for restore.
  const [restoreKeyspacesOptions, setRestoreKeyspacesOptions] = useState<RestorableTablesResp[]>(
    []
  );

  // we use two forms here, one for the main form and one for the internal form
  // internal form is used to store the selected tables and table backup type
  const { control: internalControl, watch: internalWatch, setValue: internalsetValue } = useForm<
    RestoreFormModel['source']
  >({
    defaultValues: watch('source')
  });

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.restore.source'
  });

  const currentAPIType = t(
    backupDetails?.backupType === BACKUP_API_TYPES.YSQL ? 'allDatabase' : 'allKeyspaces',
    { keyPrefix: 'backup' }
  );

  const userSelectsSingleKeyspaceToRestore = doesUserSelectsSingleKeyspaceToRestore(
    additionalBackupProps!
  );

  const classes = useStyles();

  const selectedKeySpace = watch('source.keyspace');
  const commonBackupInfo = watch('currentCommonBackupInfo');
  const selectedTables = internalWatch('selectedTables');
  const internalKeyspaceSelected = internalWatch('keyspace');
  const tableBackupType = internalWatch('tableBackupType');

  const setDefaultKeyspace = (tables: RestorableTablesResp[]) => {
    if (isDefinedNotNull(selectedKeySpace?.value)) return;
    if (userSelectsSingleKeyspaceToRestore) {
      const currentCommonBackupInfo = commonBackupInfo;

      internalsetValue('keyspace', {
        label: currentCommonBackupInfo!.responseList[0].keyspace,
        value: currentCommonBackupInfo!.responseList[0].keyspace,
        isDefaultOption: false
      });
    } else {
      const t = tables.find((rt: any) => rt.isDefaultOption) as any;
      internalsetValue('keyspace', { label: t.keyspace, value: t.keyspace, isDefaultOption: true });
    }
  };

  // fetch the list of restorable tables only when the restore is  triggered from "Restore entire backup"
  // for incremental backup, we can iterate the commonBackupInfo and get the list of keyspaces and tables
  useQuery(
    ['restorableTables', backupDetails?.commonBackupInfo.baseBackupUUID],
    () => getRestorableTables(backupDetails!.commonBackupInfo.baseBackupUUID),
    {
      enabled: !isRestoreTriggeredFromIncBackup(backupDetails!, additionalBackupProps!),
      select: (data) => [
        {
          keyspace: currentAPIType,
          isDefaultOption: true,
          tableNames: []
        },
        ...data.data
      ],
      onSuccess(data) {
        setRestoreKeyspacesOptions(data);
        setRestorableTables(data);
        setDefaultKeyspace(data);
      }
    }
  );

  useMount(() => {
    // if restore is triggered from incremental backup, we can get the list of keyspaces and tables from commonBackupInfo
    if (isRestoreTriggeredFromIncBackup(backupDetails!, additionalBackupProps!)) {
      if (commonBackupInfo) {
        const keyspaces = [
          {
            keyspace: currentAPIType,
            isDefaultOption: true, // only for select all keyspace/databse
            tableNames: []
          },
          ...commonBackupInfo.responseList.map((rt) => ({
            keyspace: rt.keyspace,
            tableNames: rt.tablesList ?? []
          }))
        ];
        setRestoreKeyspacesOptions(keyspaces);
        setRestorableTables(keyspaces);
        setDefaultKeyspace(keyspaces);
      }
    }
  });

  useEffect(() => {
    setValue('source.keyspace', internalKeyspaceSelected);
  }, [internalKeyspaceSelected]);

  useEffect(() => {
    setValue('source.selectedTables', selectedTables, { shouldValidate: true });
  }, [selectedTables]);

  // when the selected keyspace changes, update the list of tables in the selected keyspace
  useEffect(() => {
    const keyspace = restoreKeyspacesOptions?.find((rt) => rt.keyspace === selectedKeySpace?.value);
    if (!keyspace) return;
    setTablesInSelectedKeyspace(
      keyspace.tableNames.map((t) => (({ tableName: t } as unknown) as ITable))
    );
    internalsetValue('selectedTables', [], { shouldValidate: true });
    internalsetValue('tableBackupType', Backup_Options_Type.ALL, { shouldValidate: true });
  }, [selectedKeySpace]);

  useEffect(() => {
    setValue('source.tableBackupType', tableBackupType, { shouldValidate: true });
  }, [tableBackupType]);

  const reactSelectComp = ReactSelectComponents(!!errors?.source?.keyspace?.message);

  if (!backupDetails) return null;

  return (
    <div className={classes.root}>
      <Typography variant="body1">{t('selectKeyspace')}</Typography>
      <YBReactSelectField
        control={internalControl}
        name="keyspace"
        width="620px"
        options={restoreKeyspacesOptions?.map((restorableTables) => {
          return {
            label: restorableTables.keyspace,
            value: restorableTables.keyspace,
            tableNames: restorableTables.tableNames,
            isDefaultOption: !!(restorableTables as any).isDefaultOption
          };
        })}
        stylesOverride={ReactSelectStyles}
        isClearable
        isDisabled={userSelectsSingleKeyspaceToRestore}
        components={{
          ...reactSelectComp
        }}
      />
      {backupDetails?.backupType === TableType.YQL_TABLE_TYPE && selectedKeySpace && (
        <SelectTables
          control={(internalControl as unknown) as Control<FieldValues>}
          tablesInSelectedKeyspace={tablesInSelectedKeyspace}
        />
      )}
    </div>
  );
};
