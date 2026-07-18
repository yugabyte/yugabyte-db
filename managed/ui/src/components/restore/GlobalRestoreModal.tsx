import { useTranslation } from 'react-i18next';
import { makeStyles, Typography, CircularProgress } from '@material-ui/core';
import { useQuery } from 'react-query';
import { YBModal } from '@app/redesign/components';
import Cookies from 'js-cookie';

import { api, taskQueryKey } from '@app/redesign/helpers/api';
import { REFETCH_INTERVAL_MS } from '../ha/hooks/useLoadHAConfiguration';
import { TaskState, TaskType } from '@app/redesign/features/tasks/dtos';
import { SortDirection } from '@app/redesign/utils/dtos';

const useStyles = makeStyles((theme) => ({
  modalContent: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',

    padding: theme.spacing(4)
  },
  dialogContentRoot: {
    padding: theme.spacing(5)
  },
  spinner: {
    marginBottom: theme.spacing(3),
    color: theme.palette.ybacolors.accent_2_1
  },
  backupInfo: {
    display: 'flex',
    flexDirection: 'column',
    alignItems: 'center',
    gap: theme.spacing(2.5)
  }
}));

export const GlobalRestoreModal = () => {
  const classes = useStyles();
  const { t } = useTranslation('translation', { keyPrefix: 'restore.globalModal' });

  const getPagedYbaRestoreTaskRequest = {
    direction: SortDirection.DESC,
    needTotalCount: true,
    filter: {
      typeList: [TaskType.RESTORE_YBA_BACKUP],
      status: [TaskState.CREATED, TaskState.RUNNING, TaskState.INITIALIZING, TaskState.ABORT]
    }
  };
  const ybaRestoreTaskQuery = useQuery(
    taskQueryKey.paged(getPagedYbaRestoreTaskRequest),
    () => api.fetchPagedCustomerTasks(getPagedYbaRestoreTaskRequest),
    {
      refetchInterval: REFETCH_INTERVAL_MS
    }
  );

  const hasAuthToken = !!(Cookies.get('authToken') ?? localStorage.getItem('authToken'));
  const hasCustomerId = !!(Cookies.get('customerId') ?? localStorage.getItem('customerId'));
  const isUserLoggedIn = hasAuthToken && hasCustomerId;
  const ybaRestoreInProgress = !!ybaRestoreTaskQuery.data?.totalCount;
  return (
    <YBModal
      open={isUserLoggedIn && ybaRestoreInProgress}
      onClose={() => {}}
      hideCloseBtn={true}
      enableBackdropDismiss={false}
      size="sm"
      minHeight="fit-content"
      overrideHeight={300}
      overrideWidth={600}
    >
      <div className={classes.modalContent}>
        <CircularProgress size={48} className={classes.spinner} />
        <div className={classes.backupInfo}>
          <Typography variant="h4">{t('restoringPlatform')}</Typography>
          <Typography variant="body2">{t('ybaWillBeAvailable')}</Typography>
        </div>
      </div>
    </YBModal>
  );
};
