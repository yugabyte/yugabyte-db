import { toast } from 'react-toastify';
import { useTranslation } from 'react-i18next';
import { useMutation } from 'react-query';
import { Box, makeStyles } from '@material-ui/core';
import { TroubleshootAdvisor } from '@yugabytedb/troubleshoot-ui';
import { YBPanelItem } from '../../panels';
import { YBButton } from '../../../redesign/components';
import { AppName } from '../../../redesign/features/Troubleshooting/TroubleshootingDashboard';
import { TroubleshootingAPI } from '../../../redesign/features/Troubleshooting/api';

const useStyles = makeStyles((theme) => ({
  button: {
    marginLeft: theme.spacing(2),
    marginTop: theme.spacing(-0.5),
    float: 'right'
  }
}));

interface TroubleshootUniverseProps {
  universeUuid: string;
  tpUuid: string;
  appName: AppName;
  timezone: string;
  apiUrl: string;
  registrationStatus: boolean;
  refetchUniverseRegistration: any;
}

export const TroubleshootUniverse = ({
  universeUuid,
  appName,
  tpUuid,
  timezone,
  apiUrl,
  registrationStatus,
  refetchUniverseRegistration
}: TroubleshootUniverseProps) => {
  const helperClasses = useStyles();
  const { t } = useTranslation();

  const onMonitorUniverse = () => {
    monitorUniverse.mutateAsync();
  };

  // PUT API call to monitor universe
  const monitorUniverse = useMutation(
    () => TroubleshootingAPI.monitorUniverse(tpUuid, universeUuid),
    {
      onSuccess: () => {
        toast.success(t('clusterDetail.troubleshoot.monitorUniverseSuccess'));
        refetchUniverseRegistration();
      },
      onError: () => {
        toast.error(t('clusterDetail.troubleshoot.monitorUniverseFailure'));
      }
    }
  );

  return registrationStatus ? (
    <TroubleshootAdvisor
      universeUuid={universeUuid}
      appName={appName}
      timezone={timezone}
      apiUrl={apiUrl}
    />
  ) : (
    <YBPanelItem
      body={
        <Box>
          {t('clusterDetail.troubleshoot.startMonitoring')}
          <YBButton
            variant="primary"
            size="medium"
            className={helperClasses.button}
            onClick={onMonitorUniverse}
          >
            {t('clusterDetail.troubleshoot.monitor')}
          </YBButton>
        </Box>
      }
    />
  );
};
