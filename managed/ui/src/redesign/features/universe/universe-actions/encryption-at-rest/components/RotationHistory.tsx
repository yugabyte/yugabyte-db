import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { useMKRStyles, KMSHistory } from '../EncryptionAtRestUtils';
import { ybFormatDate } from '../../../../../helpers/DateUtils';

//EAR Component
interface RotationHistoryProps {
  rotationInfo: KMSHistory | null;
  lastActiveKMS?: string;
}

export const RotationHistory: FC<RotationHistoryProps> = ({ rotationInfo, lastActiveKMS }) => {
  const classes = useMKRStyles();
  const { t } = useTranslation();

  return (
    <>
      <Box mt={0.5}>
        <Typography variant="body2" className={classes.rotationInfoText}>
          {t('universeActions.encryptionAtRest.lastRotatedLabel')}
          {rotationInfo?.timestamp ? ybFormatDate(rotationInfo.timestamp) : t('common.none')}
        </Typography>
      </Box>
      {lastActiveKMS && (
        <Box mt={1}>
          <Typography variant="body2" className={classes.rotationInfoText}>
            {t('universeActions.encryptionAtRest.lastActiveKMSLabel')}
            {lastActiveKMS ?? t('common.none')}
          </Typography>
        </Box>
      )}
    </>
  );
};
