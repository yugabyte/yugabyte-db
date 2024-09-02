/*
 * Created on Tue Aug 06 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useState } from 'react';
import { Control, useController } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { isEqual } from 'lodash';
import { FormHelperText, makeStyles } from '@material-ui/core';
import { YBModal, YBRadioGroup } from '../../../../../../components';
import { BackupObjectsModel } from '../../models/IBackupObjects';
import { YBTable } from '../../../../../../../components/backupv2/components/restore/pages/selectTables/YBTable';

import { Backup_Options_Type, ITable } from '../../../../../../../components/backupv2';
import { ReactComponent as EditIcon } from '../../../../../../assets/edit-pen-orange.svg';

interface SelectTablesProps {
  control: Control<BackupObjectsModel, any>;
  tablesInSelectedKeyspace: ITable[];
}

const useStyles = makeStyles((theme) => ({
  selectedTablesCount: {
    width: '300px',
    background: '#FBFBFB',
    borderRadius: '8px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    padding: '12px',
    marginTop: '8px',
    marginLeft: '24px',
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    fontSize: '13px',
    fontWeight: 400,
    color: '#333'
  },
  editSelection: {
    display: 'flex',
    alignItems: 'center',
    color: theme.palette.ybacolors.ybIcon,
    gap: '4px',
    fontWeight: 400,
    cursor: 'pointer',
    '&:hover': {
      textDecoration: 'underline'
    }
  }
}));

const SelectTables: FC<SelectTablesProps> = ({ control, tablesInSelectedKeyspace }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.create.backupObjects'
  });

  const classes = useStyles();

  const { field: selectedKeySpace } = useController({
    control: control,
    name: 'keyspace'
  });

  const { field: tableBackupType } = useController({
    control: control,
    name: 'tableBackupType'
  });

  const [showTableSelectModal, toggleTableSelectModal] = useState(false);

  const SelectTableOptions = [
    {
      value: Backup_Options_Type.ALL,
      label: t('allTables'),
      disabled: selectedKeySpace.value?.isDefaultOption
    },
    {
      value: Backup_Options_Type.CUSTOM,
      label: t('subSetOfTables'),
      disabled: selectedKeySpace.value?.isDefaultOption
    }
  ];

  const {
    field: selectedTables,
    formState: { errors }
  } = useController({
    control: control,
    name: 'selectedTables'
  });

  return (
    <div>
      <YBRadioGroup
        options={SelectTableOptions}
        value={tableBackupType.value}
        data-testid="tableBackupType"
        onChange={(e) => {
          tableBackupType.onChange(e.target.value as Backup_Options_Type);
          if (e.target.value === Backup_Options_Type.CUSTOM) {
            toggleTableSelectModal(true);
          } else {
            selectedTables.onChange([]);
          }
        }}
      />
      {selectedTables?.value?.length > 0 && (
        <div className={classes.selectedTablesCount}>
          {t('tablesSelected', { count: selectedTables?.value?.length })}
          <span
            data-testid="editSelection"
            className={classes.editSelection}
            onClick={() => {
              toggleTableSelectModal(true);
            }}
          >
            <EditIcon />
            {t('editSelection')}
          </span>
        </div>
      )}
      {errors.selectedTables?.message && (
        <FormHelperText error>{errors.selectedTables?.message}</FormHelperText>
      )}
      <YBModal
        open={showTableSelectModal}
        onClose={() => {
          toggleTableSelectModal(false);
        }}
        overrideWidth={'1000px'}
        overrideHeight={'880px'}
        size="xl"
        style={{
          zIndex: 99999
        }}
        title={t('selectTables')}
        cancelLabel={t('cancel', { keyPrefix: 'common' })}
        submitLabel={t('confirm', { keyPrefix: 'common' })}
        onSubmit={() => {
          toggleTableSelectModal(false);
        }}
      >
        <YBTable<ITable>
          defaultValues={selectedTables.value}
          name={`universeUUIDList`}
          table={tablesInSelectedKeyspace}
          setValue={(val) => {
            if (isEqual(val, selectedTables.value)) return;
            // if the user clears the search, reset the table backup type
            if (val.length === 0) {
              tableBackupType.onChange(Backup_Options_Type.ALL);
            }
            selectedTables.onChange(val);
          }}
          tableHeader={[t('selectTablesHeader')]}
          searchPlaceholder={t('searchTableName')}
          renderBodyFn={(table) => <div>{table.tableName}</div>}
          searchFn={(table, searchText) => table.tableName.includes(searchText)}
          tableCountInfo={(selected) => (
            <>
              {selected.length} / {tablesInSelectedKeyspace?.length}&nbsp;
              {t('selected', { keyPrefix: 'common' })}
            </>
          )}
        />
      </YBModal>
    </div>
  );
};

export default SelectTables;
