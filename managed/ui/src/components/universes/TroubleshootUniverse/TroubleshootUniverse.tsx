import {
  TroubleshootAdvisor,
  TroubleshootAPI,
  QUERY_KEY,
  AttachUniverse
} from '@yugabytedb/troubleshoot-ui';
import { AppName } from '../../../redesign/features/Troubleshooting/TroubleshootingDashboard';
import { useQuery } from 'react-query';
import { useState } from 'react';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { Box, makeStyles } from '@material-ui/core';
import { useSelector } from 'react-redux';
import { api, QUERY_KEY as TOKEN_KEY } from '../../../redesign/utils/api';
import { IN_DEVELOPMENT_MODE, ROOT_URL } from '../../../config';

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
  const [showAttachUniverseDialog, setShowAttachUniverseDialog] = useState<boolean>(false);
  const baseUrl = ROOT_URL.split('/api/');
  const { currentCustomer } = useSelector((state: any) => state.customer);

  const sessionInfo = useQuery(TOKEN_KEY.getSessionInfo, () => api.getSessionInfo());
  const troubleshootingUniverseMetadata = useQuery(QUERY_KEY.fetchUniverseMetadataList, () =>
    TroubleshootAPI.fetchUniverseMetadataList()
  );

  if (troubleshootingUniverseMetadata.isError) {
    return <YBErrorIndicator />;
  }
  if (
    troubleshootingUniverseMetadata.isLoading ||
    (troubleshootingUniverseMetadata.isIdle && troubleshootingUniverseMetadata.data === undefined)
  ) {
    return <YBLoading />;
  }

  const currentUniverseMetadata = troubleshootingUniverseMetadata?.data?.find(
    (metadata) => metadata.id === universeUuid
  );

  const onAttachUniverseButtonClick = () => {
    troubleshootingUniverseMetadata.refetch();
    setShowAttachUniverseDialog(true);
  };

  const onAttachUniverseDialogClose = () => {
    troubleshootingUniverseMetadata.refetch();
    setShowAttachUniverseDialog(false);
  };

  const onAttachUniverse = () => {
    onAttachUniverseButtonClick();
  };

  return currentUniverseMetadata ? (
    <TroubleshootAdvisor universeUuid={universeUuid} appName={appName} timezone={timezone} />
  ) : (
    <Box>
      {'Universe is currently not registered to the troubleshooting service,'}
      <a onClick={onAttachUniverse} className={helperClasses.register}>
        {' please register here'}
      </a>
      {showAttachUniverseDialog && (
        <AttachUniverse
          universeUuid={universeUuid}
          customerUuid={currentCustomer.data.uuid}
          baseUrl={baseUrl[0]}
          apiToken={sessionInfo?.data?.apiToken}
          open={showAttachUniverseDialog}
          onClose={onAttachUniverseDialogClose}
          isDevMode={IN_DEVELOPMENT_MODE}
        />
      )}
    </Box>
  );
};
