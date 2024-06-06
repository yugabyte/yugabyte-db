import { useState } from 'react';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { Box, makeStyles } from '@material-ui/core';
import {
  TroubleshootAdvisor,
  TroubleshootAPI,
  QUERY_KEY,
  AttachUniverse
} from '@yugabytedb/troubleshoot-ui';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBPanelItem } from '../../panels';
import { AppName } from '../../../redesign/features/Troubleshooting/TroubleshootingDashboard';
import {
  TroubleshootingAPI,
  QUERY_KEY as TROUBLESHOOTING_QUERY_KEY
} from '../../../redesign/features/Troubleshooting/api';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import { isNonEmptyString } from '../../../utils/ObjectUtils';
import { toast } from 'react-toastify';

const STATUS = {
  SUCCESS: 'success',
  ERROR: 'error'
};

interface TroubleshootUniverseProps {
  universeUuid: string;
  appName: AppName;
  timezone: string;
}

const useStyles = makeStyles((theme) => ({
  register: {
    cursor: 'pointer'
  }
}));

export const TroubleshootUniverse = ({
  universeUuid,
  appName,
  timezone
}: TroubleshootUniverseProps) => {
  const helperClasses = useStyles();
  const [TpData, setTpData] = useState<any>([]);
  const [showAttachUniverseDialog, setShowAttachUniverseDialog] = useState<boolean>(false);
  const { currentCustomer } = useSelector((state: any) => state.customer);

  const troubleshootingUniverseMetadata = useQuery(QUERY_KEY.fetchUniverseMetadataList, () =>
    TroubleshootAPI.fetchUniverseMetadataList()
  );
  const TpList = useQuery(
    TROUBLESHOOTING_QUERY_KEY.fetchTpList,
    () => TroubleshootingAPI.fetchTpList(),
    {
      onSuccess: (data) => {
        setTpData(data);
      }
    }
  );

  if (troubleshootingUniverseMetadata.isError || TpList.isError) {
    return <YBErrorIndicator />;
  }
  if (
    troubleshootingUniverseMetadata.isLoading ||
    TpList.isLoading ||
    (troubleshootingUniverseMetadata.isIdle &&
      troubleshootingUniverseMetadata.data === undefined) ||
    (TpList.isIdle && TpList.data === undefined)
  ) {
    return <YBLoading />;
  }

  const currentUniverseMetadata = troubleshootingUniverseMetadata?.data?.find(
    (metadata) => metadata.id === universeUuid
  );

  const onAttachUniverseDialogClose = () => {
    troubleshootingUniverseMetadata.refetch();
    setShowAttachUniverseDialog(false);
  };

  const onAttachUniverse = () => {
    troubleshootingUniverseMetadata.refetch();
    setShowAttachUniverseDialog(true);
  };

  const onUpdateMetadata = (status: string) => {
    if (status === STATUS.SUCCESS) {
      toast.success('Universe is successfully added to troubleshooting service');
    } else {
      toast.error('Unable to add universe to troubleshooting service');
    }
  };

  return currentUniverseMetadata && isNonEmptyString(TpData?.[0]?.tpUrl) ? (
    <TroubleshootAdvisor
      universeUuid={universeUuid}
      appName={appName}
      timezone={timezone}
      apiUrl={`${TpData[0].tpUrl}/api`}
    />
  ) : (
    <YBPanelItem
      body={
        isNonEmptyString(TpData?.[0]?.tpUrl) ? (
          <Box>
            {'Universe is currently not registered to the troubleshooting service, '}
            <a onClick={onAttachUniverse} className={helperClasses.register}>
              {' please register here'}
            </a>
            {showAttachUniverseDialog && (
              <AttachUniverse
                universeUuid={universeUuid}
                customerUuid={currentCustomer.data.uuid}
                platformUrl={TpData?.[0]?.ybaUrl}
                apiUrl={`${TpData?.[0]?.tpUrl}/api`}
                metricsUrl={TpData?.[0]?.metricsUrl}
                open={showAttachUniverseDialog}
                onUpdateMetadata={onUpdateMetadata}
                onClose={onAttachUniverseDialogClose}
                isDevMode={IN_DEVELOPMENT_MODE}
              />
            )}
          </Box>
        ) : (
          <Box>
            {'Please'}
            <a href={`/config/troubleshoot/config`} className={helperClasses.register}>
              {' register '}
            </a>
            {'YB Anywhere instance to Troubleshooting Platform Service'}
          </Box>
        )
      }
    />
  );
};
