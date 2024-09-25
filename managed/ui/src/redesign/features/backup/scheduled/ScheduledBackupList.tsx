/*
 * Created on Wed Jul 17 2024
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useMemo } from 'react';
import { useInfiniteQuery } from 'react-query';
import { keyBy } from 'lodash';
import { useTranslation } from 'react-i18next';
import { useSelector } from 'react-redux';
import { useToggle } from 'react-use';
import { Grid, makeStyles } from '@material-ui/core';
import { YBButton } from '../../../components';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';
import InfiniteScroll from 'react-infinite-scroll-component';
import CreateScheduledBackupModal from './create/CreateScheduledBackupModal';
import { BACKUP_REFETCH_INTERVAL } from '../../../../components/backupv2/common/BackupUtils';
import { ScheduledCard } from './list/ScheduledCard';
import { ScheduledBackupEmpty } from './ScheduledBackupEmpty';
import { AllowedTasks } from '../../../helpers/dtos';
import { getScheduledBackupList } from '../../../../components/backupv2/common/BackupScheduleAPI';

interface ScheduledBackupListProps {
  universeUUID: string;
  allowedTasks: AllowedTasks;
}

const useStyles = makeStyles((theme) => ({
  noMoreBackup: {
    display: 'flex',
    justifyContent: 'center',
    marginTop: '32px',
    opacity: 0.5
  },
  cardList: {
    display: 'flex',
    flexDirection: 'column',
    gap: '32px'
  }
}));

const ScheduledBackupList: FC<ScheduledBackupListProps> = ({ universeUUID }) => {
  const [createScheduledBackupModalVisible, toggleCreateScheduledBackupModalVisible] = useToggle(
    false
  );

  const { data: scheduledBackupList, isLoading, fetchNextPage, hasNextPage } = useInfiniteQuery(
    ['scheduled_backup_list'],
    ({ pageParam = 0 }) => getScheduledBackupList(pageParam, universeUUID),
    {
      getNextPageParam: (lastPage) => lastPage.data.hasNext,
      refetchInterval: BACKUP_REFETCH_INTERVAL
    }
  );

  const storageConfigs = useSelector((reduxState: any) => reduxState.customer.configs);

  const storageConfigsMap = useMemo(() => keyBy(storageConfigs?.data ?? [], 'configUUID'), [
    storageConfigs
  ]);

  const { t } = useTranslation('translation', {
    keyPrefix: 'backup.scheduled.list'
  });

  const classes = useStyles();

  if (isLoading) {
    return <YBLoadingCircleIcon />;
  }

  const schedules = scheduledBackupList?.pages
    .flatMap((page) => {
      return page.data.entities;
    })
    .filter(
      (schedule) =>
        schedule.backupInfo !== undefined && schedule.backupInfo.universeUUID === universeUUID
    );

  if (!schedules || schedules?.length === 0) {
    return (
      <div>
        <ScheduledBackupEmpty
          hasPerm={true}
          onActionButtonClick={() => {
            toggleCreateScheduledBackupModalVisible(true);
          }}
        />
        <CreateScheduledBackupModal
          visible={createScheduledBackupModalVisible}
          onHide={() => {
            toggleCreateScheduledBackupModalVisible(false);
          }}
        />
      </div>
    );
  }

  return (
    <InfiniteScroll
      dataLength={2}
      hasMore={!!hasNextPage}
      next={fetchNextPage}
      loader={<YBLoadingCircleIcon />}
      endMessage={<div className={classes.noMoreBackup}>{t('noMoreSchedules')}</div>}
      height={'550px'}
    >
      <div className={classes.cardList}>
        <Grid container spacing={2} justifyContent="flex-end">
          <Grid item>
            <YBButton
              variant="primary"
              onClick={() => {
                toggleCreateScheduledBackupModalVisible(true);
              }}
            >
              {t('createPolicy')}
            </YBButton>
          </Grid>
        </Grid>
        {schedules.map((schedule) => (
          <ScheduledCard
            key={schedule.scheduleUUID}
            schedule={schedule}
            universeUUID={universeUUID}
            storageConfig={storageConfigsMap[schedule.backupInfo.storageConfigUUID]}
          />
        ))}
        <CreateScheduledBackupModal
          visible={createScheduledBackupModalVisible}
          onHide={() => {
            toggleCreateScheduledBackupModalVisible(false);
          }}
        />
      </div>
    </InfiniteScroll>
  );
};

export default ScheduledBackupList;
