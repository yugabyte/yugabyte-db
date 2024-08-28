import { Box } from '@material-ui/core';
import { useEffect, useState } from 'react';
import { useInterval } from 'react-use';
import { toast } from 'react-toastify';
import { MasterFailoverWarningBanner } from './components/MasterFailoverWarningBanner';
import { PostFailoverWarningBanner } from './components/PostFailoverWarningBanner';
import {
  usePageListJobSchedules,
  useSnoozeJobSchedule
} from '../../../../../v2/api/job-scheduler/job-scheduler';
import {
  JobSchedule,
  JobSchedulePagedResp,
  JobScheduleState
} from '../../../../../v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { Universe } from '../../universe-form/utils/dto';
import { FailoverJobType, getDiffUTCDuration } from './MasterFailoverUtils';
import { isNonEmptyObject } from '../../../../../utils/ObjectUtils';
import { use } from 'i18next';

interface MasterFailoverProps {
  universeData: Universe;
}

export const MasterFailover = ({ universeData }: MasterFailoverProps) => {
  const [masterFailoverJob, setMasterFailoverJob] = useState<JobSchedule | null>(null);
  const [postFailoverJob, setPostFailoverJob] = useState<JobSchedule | null>(null);
  const [masterFailoverSchedule, setMasterFailoverSchedule] = useState<string>('');
  const [postFailoverSchedule, setPostFailoverSchedule] = useState<string>('');
  const [jUuid, setJUuid] = useState<string>('');

  // To get the master failover job schedule or the post failover job schedule
  const materFailoverJobSchedule = usePageListJobSchedules({
    mutation: {
      onSuccess: (data: JobSchedulePagedResp) => {
        const masterFailoverJob = data.entities?.find(
          (entity: JobSchedule) =>
            entity?.spec?.job_config?.config?.failoverJobType === FailoverJobType.MASTER_FAILOVER &&
            entity.spec.job_config.config.universeUuid === universeData.universeUUID
        );
        const postFailoverJob = data.entities?.find(
          (entity: JobSchedule) =>
            entity?.spec?.job_config?.config?.failoverJobType ===
              FailoverJobType.SYNC_MASTER_ADDRS &&
            entity.spec.job_config.config.universeUuid === universeData.universeUUID
        );
        if (
          masterFailoverJob?.info?.state === JobScheduleState.INACTIVE &&
          !masterFailoverJob?.spec?.schedule_config?.disabled
        ) {
          const nextStartTime = masterFailoverJob.info.next_start_time;
          const diffDuration = getDiffUTCDuration(nextStartTime!);
          setMasterFailoverSchedule(diffDuration);
          setMasterFailoverJob(masterFailoverJob);
          setJUuid(masterFailoverJob.info.uuid!);
        } else if (
          masterFailoverJob?.info?.state === JobScheduleState.ACTIVE ||
          masterFailoverJob?.spec?.schedule_config?.disabled
        ) {
          onSetEmptyMasterFailoverJobParams();
        }

        if (
          postFailoverJob?.info?.state === JobScheduleState.INACTIVE &&
          !postFailoverJob?.spec?.schedule_config?.disabled
        ) {
          const nextStartTime = postFailoverJob.info.next_start_time;
          const diffDuration = getDiffUTCDuration(nextStartTime!);
          setPostFailoverSchedule(diffDuration);
          setPostFailoverJob(postFailoverJob);
          setJUuid(postFailoverJob.info.uuid!);
        } else if (
          postFailoverJob?.info?.state === JobScheduleState.ACTIVE ||
          postFailoverJob?.spec?.schedule_config?.disabled
        ) {
          onSetEmptyPostFailoverJobParams();
        }
      }
    }
  });

  const onSetEmptyMasterFailoverJobParams = () => {
    setMasterFailoverSchedule('');
    setMasterFailoverJob(null);
    setJUuid('');
  };

  const onSetEmptyPostFailoverJobParams = () => {
    setPostFailoverSchedule('');
    setPostFailoverJob(null);
    setJUuid('');
  };

  // To snooze failover schedule
  const snoozeFailoverSchedule = useSnoozeJobSchedule({
    mutation: {
      onSuccess: () => {
        toast.success('Failover schedule snoozed successfully');
        onWarningBannerChangeDuration();
      },
      onError: () => {
        toast.success('Unable to snooze the failover schedule');
      }
    }
  });

  const onSnoozeClick = () => {
    snoozeFailoverSchedule.mutateAsync({ jUUID: jUuid, data: { snooze_secs: 3600 } });
  };

  // Once the scheduled is snoozed, we need to update the banner with the new duration
  const onWarningBannerChangeDuration = () => {
    materFailoverJobSchedule.mutateAsync({
      data: { filter: { name_regex: `AutoMasterFailover_${universeData.universeUUID}` } }
    });
  };

  // Call the API during the initial mount of the component
  useEffect(() => {
    materFailoverJobSchedule.mutateAsync({
      data: { filter: { name_regex: `AutoMasterFailover_${universeData.universeUUID}` } }
    });
  }, []);

  // Check if master failover is detected via the API every 30 seconds
  useInterval(() => {
    materFailoverJobSchedule.mutateAsync({
      data: { filter: { name_regex: `AutoMasterFailover_${universeData.universeUUID}` } }
    });
  }, 30000);

  return (
    <Box>
      {isNonEmptyObject(masterFailoverJob) && (
        <MasterFailoverWarningBanner
          duration={masterFailoverSchedule}
          onSnoozeClick={onSnoozeClick}
        />
      )}
      {isNonEmptyObject(postFailoverJob) && (
        <PostFailoverWarningBanner duration={postFailoverSchedule} onSnoozeClick={onSnoozeClick} />
      )}
    </Box>
  );
};
