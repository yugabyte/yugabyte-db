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
import { api, QUERY_KEY as TOKEN_KEY } from '../../../redesign/utils/api';
import {
  TroubleshootingAPI,
  QUERY_KEY as TROUBLESHOOTING_QUERY_KEY
} from '../../../redesign/features/Troubleshooting/api';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import { isNonEmptyString } from '../../../utils/ObjectUtils';

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

  const sessionInfo = useQuery(TOKEN_KEY.getSessionInfo, () => api.getSessionInfo());
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
                apiToken={sessionInfo?.data?.apiToken}
                open={showAttachUniverseDialog}
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
