import { FC } from 'react';
import { styled, Typography } from '@material-ui/core';
import { HelpOutline } from '@material-ui/icons';
import { YBModal, mui } from '@yugabyte-ui-library/core';
import { Trans, useTranslation } from 'react-i18next';
import BookIcon from '../../../../../assets/documentation.svg';
import RFHelpPng from '../../../../../assets/rfFactorHelp.png';

const { Box } = mui;

export interface ReplicationFactorInfoProps {
  open: boolean;
  onClose: () => void;
}
const HelpIcon = styled(HelpOutline)(({ theme }) => ({
  width: '24px',
  height: '24px'
}));
const Highlight = styled('span')(({ theme }) => ({
  color: theme.palette.primary[700],
  fontWeight: 400,
  fontSize: '11.5px',
  borderRadius: '4px',
  backgroundColor: theme.palette.primary[200],
  padding: '2px 6px'
}));

const InfoPanel = styled(Box)(({ theme }) => ({
  marginTop: '16px',
  padding: '16px',
  display: 'flex',
  flexDirection: 'column',
  gap: '16px',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  background: '#FBFCFD',
  color: theme.palette.grey[700]
}));

const Link = styled('a')(({ theme }) => ({
  color: theme.palette.primary[600],
  textDecoration: 'underline',
  cursor: 'pointer',
  '&:hover': {
    textDecoration: 'none'
  }
}));

const HelpImage = styled('img')(({ theme }) => ({
  maskImage: `linear-gradient(to right, transparent 5%, black 75%)`
}));

export const ReplicationFactorInfoModal: FC<ReplicationFactorInfoProps> = ({ open, onClose }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.replicationFactorInfoModal'
  });

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('title')}
      titleIcon={<HelpIcon />}
      titleSeparator
      overrideHeight={540}
      overrideWidth={600}
    >
      <Box sx={{ padding: '16px 8px', display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <Box style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
          <Typography variant="body1">{t('rfFactor')}</Typography>
          <Typography variant="body2">{t('rfFactorAns')}</Typography>
        </Box>
        <Box style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
          <Typography variant="body1">{t('FtAndRf')}</Typography>
          <Typography variant="body2">
            <Trans t={t} i18nKey="FtAndRfAns" components={{ highlight: <Highlight /> }} />
          </Typography>
        </Box>
        <InfoPanel>
          <Box>
            <Trans t={t} i18nKey="tip" components={{ b: <b /> }} />
          </Box>
          <HelpImage src={RFHelpPng} />
        </InfoPanel>
        <Box sx={{ display: 'flex', gap: '4px', alignItems: 'center', marginLeft: '8px' }}>
          <BookIcon />
          <Trans
            t={t}
            style={{ fontSize: '11.5px', fontWeight: 400 }}
            i18nKey="learn"
            components={{
              a: <Link />,
              a2: <Link />
            }}
          />
        </Box>
      </Box>
    </YBModal>
  );
};
