import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { YBModal, mui } from '@yugabyte-ui-library/core';

//icons
import { HelpOutline } from '@material-ui/icons';
import BookIcon from '../../../../../assets/documentation.svg';

export interface ResilienceTooltipProps {
  onClose: () => void;
  open: boolean;
}

const { styled, Typography } = mui;

const Link = styled('a')(({ theme }) => ({
  color: theme.palette.primary[600],
  textDecoration: 'underline',
  cursor: 'pointer',
  '&:hover': {
    textDecoration: 'none'
  }
}));

const HelpIcon = styled(HelpOutline)(({ theme }) => ({
  width: '24px',
  height: '24px'
}));

export const ResilienceTooltip: FC<ResilienceTooltipProps> = ({ onClose, open }) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'createUniverseV2.resilienceAndRegions.infoTooltips.resilience'
  });
  return (
    <YBModal
      open={open}
      overrideHeight={'auto'}
      overrideWidth={600}
      titleSeparator
      titleIcon={<HelpIcon />}
      onClose={onClose}
      title={t('title')}
    >
      <div style={{ padding: '16px 8px', display: 'flex', flexDirection: 'column', gap: '16px' }}>
        <Typography variant="body1">{t('header')}</Typography>
        <span className="title">{t('msg')}</span>
      </div>
      <div
        style={{
          display: 'flex',
          gap: '4px',
          alignItems: 'center',
          margin: '8px 0px 16px 8px'
        }}
      >
        <BookIcon />
        <Link>{t('learnMore')}</Link>
      </div>
    </YBModal>
  );
};
