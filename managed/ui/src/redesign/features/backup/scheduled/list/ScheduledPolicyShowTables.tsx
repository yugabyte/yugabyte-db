/*
 * Created on Tue Sep 10 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { YBModal } from '../../../../components';
import { YBTable } from '../../../../../components/common/YBTable';
import { IBackupSchedule } from '../../../../../components/backupv2';

interface ScheduledPolicyShowTablesProps {
  schedule: IBackupSchedule;
  visible: boolean;
  onHide: () => void;
}

const ScheduledPolicyShowTables: FC<ScheduledPolicyShowTablesProps> = ({
  visible,
  onHide,
  schedule
}) => {
  const tables = schedule.backupInfo?.keyspaceList?.[0]?.tablesList?.map((t) => ({
    table: t
  }));

  const { t } = useTranslation('translation', { keyPrefix: 'backup.scheduled.list' });

  return (
    <YBModal
      open={visible}
      onClose={onHide}
      overrideWidth={'600px'}
      overrideHeight={'750px'}
      cancelLabel={t('close', { keyPrefix: 'common' })}
      title={t('backupTables')}
    >
      <YBTable
        data={(tables as any) ?? []}
        pagination
        options={{
          paginationSize: 2,
          hideSizePerPage: true
        }}
      >
        <TableHeaderColumn isKey dataField="table">
          Tables
        </TableHeaderColumn>
      </YBTable>
    </YBModal>
  );
};

export default ScheduledPolicyShowTables;
