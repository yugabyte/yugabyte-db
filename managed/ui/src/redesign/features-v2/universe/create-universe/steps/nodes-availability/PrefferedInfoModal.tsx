import { FC } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { YBModal, mui } from '@yugabyte-ui-library/core';

//icons
import { HelpOutline } from '@material-ui/icons';
import BookIcon from '../../../../../assets/blue-book.svg';

const { Box, Typography, styled } = mui;

interface PreferredInfoProps {
  open: boolean;
  onClose: () => void;
}

const HelpIcon = styled(HelpOutline)(() => ({
  width: '24px',
  height: '24px'
}));

const Link = styled('a')(({ theme }) => ({
  color: theme.palette.primary[600],
  textDecoration: 'underline',
  cursor: 'pointer',
  '&:hover': {
    textDecoration: 'none'
  }
}));

const StyledUL = styled('ul')(({ theme }) => ({
  padding: '0',
  color: theme.palette.grey[700],
  listStyleType: 'disc',
  lineHeight: '20px',
  fontSize: theme.typography.subtitle1.fontSize,
  '&>li': {
    marginLeft: '16px'
  }
}));

const InfoArea = styled(Box)(({ theme }) => ({
  padding: '16px',
  display: 'flex',
  flexDirection: 'column',
  gap: '16px',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px',
  background: '#FBFCFD',
  color: theme.palette.grey[700]
}));

export const PreferredInfoModal: FC<PreferredInfoProps> = ({ open, onClose }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.nodesAndAvailability.prefferedInfoModal'
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
      <div style={{ padding: '16px 8px', display: 'flex', flexDirection: 'column', gap: '24px' }}>
        <Typography variant="body1">{t('prefferedQues')}</Typography>
        <Typography variant="body2">{t('prefferedAns')}</Typography>
        <Box sx={{ display: 'flex', gap: '4px', alignItems: 'center' }}>
          <BookIcon />
          <Trans
            t={t}
            style={{ fontSize: '11.5px', fontWeight: 400 }}
            i18nKey="links"
            components={{
              a1: <Link />,
              a2: <Link />
            }}
          />
        </Box>
        <InfoArea>
          <StyledUL>
            <Trans
              t={t}
              style={{ fontSize: '11.5px', fontWeight: 400 }}
              i18nKey="note"
              components={{
                b: <b />,
                a: <Link />,
                li: <li />,
                br: <br />
              }}
            />
          </StyledUL>
        </InfoArea>
      </div>
    </YBModal>
  );
};
