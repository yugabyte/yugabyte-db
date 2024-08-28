/*
 * Created on Thu Jul 18 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { forwardRef, useContext, useImperativeHandle } from 'react';
import {
  Page,
  PageRef,
  ScheduledBackupContext,
  ScheduledBackupContextMethods
} from '../../models/ScheduledBackupContext';
import { useForm } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { yupResolver } from '@hookform/resolvers/yup';
import { groupBy, sortBy, uniqBy, values } from 'lodash';

import { makeStyles, Typography } from '@material-ui/core';
import { BackupObjectsModel } from '../../models/IBackupObjects';
import { YBTag, YBTag_Types } from '../../../../../../../components/common/YBTag';
import { YBErrorIndicator, YBLoading } from '../../../../../../../components/common/indicators';
import { BackupTablespace } from './BackupTablespace';
import SelectTables from './SelectTables';
import { ReactSelectComponents, ReactSelectStyles } from '../../ReactSelectStyles';
import { YBReactSelectField } from '../../../../../../../components/configRedesign/providerRedesign/components/YBReactSelect/YBReactSelectField';

import { GetUniverseUUID } from '../../../ScheduledBackupUtils';
import { fetchTablesInUniverse } from '../../../../../../../actions/xClusterReplication';

import { BACKUP_API_TYPES, ITable } from '../../../../../../../components/backupv2';
import { getValidationSchema } from './BackupObjectsValidation';

const useStyles = makeStyles(() => ({
  root: {
    padding: '24px',
    display: 'flex',
    gap: '24px',
    flexDirection: 'column'
  },
  selectDB: {
    marginBottom: '6px'
  }
}));

const BackupObjects = forwardRef<PageRef>((_, forwardRef) => {
  const [scheduledBackupContext, { setPage, setBackupObjects }] = (useContext(
    ScheduledBackupContext
  ) as unknown) as ScheduledBackupContextMethods;

  const classes = useStyles();

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.backupObjects'
  });

  const Default_Keyspace_database_options = [
    {
      keySpace: t('allDatabase'),
      tableType: BACKUP_API_TYPES.YSQL,
      isDefaultOption: true
    },
    {
      keySpace: t('allKeyspaces'),
      tableType: BACKUP_API_TYPES.YCQL,
      isDefaultOption: true
    }
  ];

  const {
    control,
    setValue,
    watch,
    handleSubmit,
    formState: { errors }
  } = useForm<BackupObjectsModel>({
    resolver: yupResolver(getValidationSchema(scheduledBackupContext, t)),
    defaultValues: scheduledBackupContext.formData.backupObjects
  });

  const currentUniverseUUID = GetUniverseUUID();

  const { data: tablesInUniverse, isLoading: isTableListLoading, isError } = useQuery(
    [currentUniverseUUID, 'tables'],
    () => fetchTablesInUniverse(currentUniverseUUID!),
    {
      select: (data) => data.data as ITable[]
    }
  );

  useImperativeHandle(
    forwardRef,
    () => ({
      onNext: () => {
        handleSubmit((values) => {
          setBackupObjects(values);
          setPage(Page.BACKUP_FREQUENCY);
        })();
      },
      onPrev: () => {
        setPage(Page.GENERAL_SETTINGS);
      }
    }),
    []
  );

  const reactSelecctComp = ReactSelectComponents(!!errors.keyspace?.message);

  if (isTableListLoading || !tablesInUniverse)
    return (
      <YBLoading text={t('loadingMsg.fetchingTables', { keyPrefix: 'backup.scheduled.create' })} />
    );

  if (isError)
    return (
      <div className={classes.root}>
        <YBErrorIndicator
          type="tables"
          customErrorMessage={t('errMsg.unableToFetchTables', {
            keyPrefix: 'backup.scheduled.create'
          })}
        />
      </div>
    );

  const groupByTableType = groupBy(
    [
      ...Default_Keyspace_database_options,
      ...uniqBy(tablesInUniverse, (table: ITable) => table.keySpace)
    ],
    (t) => t.tableType
  );

  const selectedKeyspace = watch('keyspace');

  return (
    <div className={classes.root}>
      <Typography variant="body1">{t('selectDBTitle')}</Typography>
      <div>
        <div className={classes.selectDB}>{t('selectDB')}</div>
        <YBReactSelectField
          width="550px"
          name="keyspace"
          control={control}
          isClearable
          stylesOverride={ReactSelectStyles}
          data-testid="keyspace"
          options={values(groupByTableType).map((options) => ({
            label: t(options[0].tableType === BACKUP_API_TYPES.YSQL ? 'Ysql' : 'Ycql', {
              keyPrefix: 'backup'
            }),
            options: sortBy(options, ['isDefaultOption', 'keySpace']).map((option) => ({
              value: option.keySpace,
              label: option.keySpace,
              isDefaultOption: 'isDefaultOption' in option ? option.isDefaultOption : false,
              tableType: option.tableType
            }))
          }))}
          onChange={(selectedOption) => {
            setValue('keyspace', (selectedOption as unknown) as BackupObjectsModel['keyspace'], {
              shouldValidate: true
            });
          }}
          defaultValue={selectedKeyspace}
          components={{
            SingleValue: function singleValue({ data }: { data: any }) {
              return (
                <>
                  <span className="storage-cfg-name">{data.label}</span> &nbsp;
                  <YBTag type={YBTag_Types.YB_GRAY}>
                    {t(data.tableType === BACKUP_API_TYPES.YSQL ? 'Ysql' : 'Ycql', {
                      keyPrefix: 'backup'
                    })}
                  </YBTag>
                </>
              );
            },
            ...reactSelecctComp
          }}
        />
      </div>
      {selectedKeyspace?.tableType === BACKUP_API_TYPES.YSQL && (
        <BackupTablespace control={control} />
      )}
      {selectedKeyspace?.tableType === BACKUP_API_TYPES.YCQL && (
        <SelectTables
          control={control}
          tablesInSelectedKeyspace={
            selectedKeyspace?.isDefaultOption
              ? []
              : tablesInUniverse?.filter((t) => t.keySpace === selectedKeyspace.value)
          }
        />
      )}
    </div>
  );
});

BackupObjects.displayName = 'BackupObjects';

export default BackupObjects;
