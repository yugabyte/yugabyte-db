import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { styled, Typography } from '@material-ui/core';
import { YBModal, mui } from '@yugabyte-ui-library/core';

//icons
import { HelpOutline } from '@material-ui/icons';
import BookIcon from '../../../../../assets/blue-book.svg';
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
  gap: '24px',
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

const StyledTypography = styled(Typography)(({ theme }) => ({
  lineHeight: '22px'
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
      overrideHeight={'auto'}
      overrideWidth={600}
    >
      <Box
        sx={{
          padding: '16px 8px',
          display: 'flex',
          flexDirection: 'column',
          gap: '24px'
        }}
      >
        <Box style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
          <StyledTypography variant="body1">{t('rfFactor')}</StyledTypography>
          <StyledTypography variant="body2">{t('rfFactorAns')}</StyledTypography>
        </Box>
        <Box style={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
          <StyledTypography variant="body1">{t('FtAndRf')}</StyledTypography>
          <StyledTypography variant="body2">
            <Trans t={t} i18nKey="FtAndRfAns" components={{ highlight: <Highlight /> }} />
          </StyledTypography>
        </Box>
        <InfoPanel>
          <Box>
            <Trans t={t} i18nKey="tip" components={{ b: <b /> }} />
          </Box>
          <HelpImage src={RFHelpPng} />
        </InfoPanel>
        <Box sx={{ display: 'flex', gap: '4px', alignItems: 'center' }}>
          <BookIcon />
          <Trans
            t={t}
            style={{ fontSize: '11.5px', fontWeight: 400, lineHeight: '16px' }}
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
