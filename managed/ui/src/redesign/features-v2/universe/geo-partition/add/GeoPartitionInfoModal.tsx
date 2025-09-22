import { useLocalStorage } from 'react-use';
import { YBModal } from '@yugabyte-ui-library/core';
import { Trans, useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';

import { ReactComponent as CloseIcon } from '@app/redesign/assets/close rounded inverted.svg';
import GeoPartitionHelpImage from '@app/redesign/assets/geoPartition.png';
import { ReactComponent as BookIcon } from '@app/redesign/assets/book_open_blue.svg';

const { styled, Box, Typography } = mui;

const ModalHeader = styled('span')(() => ({
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  '& > div': {
    display: 'flex',
    alignItems: 'center',
    fontSize: '32px',
    fontWeight: '600',
    gap: '16px',
    '& > span': {
      fontSize: '15px',
      color: '#5D5FEF'
    }
  },
  '& > svg': {
    justifySelf: 'flex-end',
    cursor: 'pointer'
  }
}));

export const GeoPartitionInfoModal = () => {
  const { t } = useTranslation('translation', { keyPrefix: 'geoPartition.GeoPartitionInfoModal' });

  const [alreadyViewed, setAlreadyViewed] = useLocalStorage(
    '__yb_geo_partition_intro_dialog__',
    false,
    {
      raw: false,
      deserializer: (value) => value === 'true',
      serializer: (value) => value.toString()
    }
  );

  if (alreadyViewed) return null;

  return (
    <YBModal
      customTitle={
        <ModalHeader role="img" aria-label="hand wave">
          <div>
            👋
            <span>{t('title')}</span>
          </div>
          <CloseIcon
            onClick={() => {
              setAlreadyViewed(true);
            }}
          />
        </ModalHeader>
      }
      open={!alreadyViewed}
      dialogContentProps={{
        dividers: true,
        sx: {
          padding: '24px !important'
        }
      }}
      size="xl"
      overrideWidth={800}
      overrideHeight={600}
      submitLabel="Got It"
      onSubmit={() => setAlreadyViewed(true)}
      buttonProps={{
        primary: {
          variant: 'secondary',
          dataTestId: 'geo-partition-info-modal-close'
        }
      }}
    >
      <Box sx={{ display: 'flex', flexDirection: 'column', gap: '16px' }}>
        <div>
          <Trans t={t} i18nKey={'description'} components={{ b: <b /> }} />
        </div>
        <img
          src={GeoPartitionHelpImage}
          alt="Geo Partition Help"
          style={{ width: '100%', borderRadius: '8px' }}
        />
        <Box
          sx={{
            display: 'flex',
            padding: '16px 24px',
            flexDirection: 'column',
            alignItems: 'flex-start',
            gap: '4px',
            borderRadius: '8px',
            border: '1px solid #CBCCFB',
            color: '#4E5F6D'
          }}
        >
          <Box
            sx={{
              display: 'flex',
              gap: '8px',
              alignItems: 'center'
            }}
          >
            <BookIcon />
            <Typography variant="body1" color="textSecondary">
              {t('desc_ques')}
            </Typography>
          </Box>
          <Typography variant="body2">
            <Trans
              t={t}
              i18nKey={'desc_ans'}
              components={{ a: <a href="#" style={{ color: '#2B59C3', cursor: 'pointer' }} /> }}
            />
          </Typography>
        </Box>
      </Box>
    </YBModal>
  );
};
