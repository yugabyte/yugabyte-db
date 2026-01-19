import { useContext } from 'react';
import { useToggle } from 'react-use';
import { YBModal, YBTag } from '@yugabyte-ui-library/core';
import { mui } from '@yugabyte-ui-library/core';
import { Trans, useTranslation } from 'react-i18next';
import { AddGeoPartitionContext, AddGeoPartitionContextMethods } from './AddGeoPartitionContext';

import CloseIcon from '@app/redesign/assets/close rounded inverted.svg';
import BookIcon from '@app/redesign/assets/book_open_blue.svg';
import Marker from '@app/redesign/assets/marker.svg';
import Designated from '@app/redesign/assets/geo_partition_designated.svg';

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

const RegionPanel = styled(Box)(({ theme }) => ({
  display: 'flex',
  gap: '16px',
  alignItems: 'center',
  justifyContent: 'center',
  padding: '56px 32px',
  background: '#FBFCFD',
  border: `1px solid ${theme.palette.grey[300]}`,
  borderRadius: '8px'
}));

const RegionCard = styled('div')(({ theme, active }) => ({
  padding: '40px 24px 24px 24px',
  display: 'flex',
  flexDirection: 'column',
  alignItems: 'center',
  gap: '16px',
  borderRadius: '8px',
  border: `1px solid ${active ? theme.palette.common.periwinkle : theme.palette.grey[300]}`,
  background: '#F7F9FB',
  width: '205px',
  position: 'relative',
  color: theme.palette.grey[700],
  '& > .header': {
    position: 'absolute',
    top: '-16px',
    padding: '8px 12px',
    borderRadius: '16px',
    border: `1px solid ${active ? theme.palette.common.periwinkle : theme.palette.grey[300]}`,
    background: '#F7F9FB',
    height: '32px',
    minWidth: '120px',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center',
    color: active ? theme.palette.common.purpleDark : theme.palette.grey[600],
    fontWeight: '600',
    fontSize: '11.5px'
  }
}));

const RegionItem = styled('div')(() => ({
  display: 'flex',
  flexDirection: 'row',
  alignItems: 'center',
  gap: '8px'
}));

export const GeoPartitionInfoModal = () => {
  const { t } = useTranslation('translation', { keyPrefix: 'geoPartition.GeoPartitionInfoModal' });

  const [addGeoPartitionContext, addGeoPartitionMethods] = (useContext(
    AddGeoPartitionContext
  ) as unknown) as AddGeoPartitionContextMethods;

  const { isNewGeoPartition, geoPartitions } = addGeoPartitionContext;

  const [alreadyViewed, setAlreadyViewed] = useToggle(!isNewGeoPartition);

  const currentGeoPartition = geoPartitions[0];

  if (alreadyViewed || !isNewGeoPartition) return null;

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
        <RegionPanel>
          <RegionCard>
            <span className="header">{t('existingRegions')}</span>
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'column',
                gap: '8px',
                alignItems: 'flex-start'
              }}
            >
              {currentGeoPartition?.resilience?.regions.map((region) => (
                <RegionItem key={region.uuid}>
                  <Marker style={{ minWidth: '16px', alignSelf: 'flex-start' }} />
                  <span>
                    {region.name} ({region.code})
                  </span>
                </RegionItem>
              ))}
            </Box>
          </RegionCard>
          <Designated />
          <RegionCard active>
            <RegionItem className="header">
              {t('geoPartition1')}
              <YBTag color="primary" size="small">
                {t('primary')}
              </YBTag>
            </RegionItem>
            <Box
              sx={{
                display: 'flex',
                flexDirection: 'column',
                gap: '8px',
                alignItems: 'flex-start'
              }}
            >
              {currentGeoPartition?.resilience?.regions.map((region) => (
                <RegionItem key={region.uuid}>
                  <Marker style={{ minWidth: '16px', alignSelf: 'flex-start' }} />
                  <span>
                    {region.name} ({region.code})
                  </span>
                </RegionItem>
              ))}
            </Box>
          </RegionCard>
        </RegionPanel>
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
