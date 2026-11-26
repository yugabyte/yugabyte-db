import { FC } from 'react';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { YBButton, YBModal, mui } from '@yugabyte-ui-library/core';

import { YBLoadingCircleIcon } from '@app/components/common/indicators';
import { api, universeQueryKey } from '@app/redesign/helpers/api';
import { DEFAULT_RELEASE_NOTES_URL } from './HelperComponent';

const { Box, Typography, styled } = mui;

/**
 * The `disc` marker is drawn with the list item's font, which renders as a square-ish
 * glyph in some font fallbacks. Draw the bullet instead so it is always a circle.
 * Geometry matches Figma: 4px dot, 19.5px indent, centered on the 18px line.
 */
const BulletList = styled('ul')(() => ({
  margin: 0,
  padding: 0,
  listStyle: 'none',
  fontFamily: 'Inter, sans-serif',
  fontSize: 13,
  fontWeight: 400,
  lineHeight: '18px',
  color: '#0B1117',
  '& > li': {
    position: 'relative',
    paddingLeft: '19.5px',
    '&::before': {
      content: '""',
      position: 'absolute',
      top: '7px',
      left: '8px',
      width: '4px',
      height: '4px',
      borderRadius: '50%',
      backgroundColor: 'currentColor'
    }
  }
}));

interface UnsupportedFeatureWarningModalProps {
  open: boolean;
  onClose: () => void;
  onSwitchBack: () => void;
}

export const UnsupportedFeatureWarningModal: FC<UnsupportedFeatureWarningModalProps> = ({
  open,
  onClose,
  onSwitchBack
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.unsupportedFeatureWarningModal'
  });
  const universeListQuery = useQuery(universeQueryKey.ALL, () => api.fetchUniverseList(), {
    enabled: open
  });

  const isUniversesReady = !universeListQuery.isLoading && !universeListQuery.isIdle;
  const universeNames = universeListQuery.data?.map((universe) => universe.name) ?? [];

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('title')}
      titleSeparator
      size="md"
      overrideWidth={600}
      overrideHeight="auto"
      hideCloseBtn={false}
      submitLabel={t('switchBack')}
      dialogContentProps={{
        dividers: false,
        sx: {
          padding: '20px 16px 32px !important',
          backgroundColor: '#FFFFFF'
        }
      }}
      customButtonTemplate={
        <Box sx={{ display: 'flex', justifyContent: 'flex-end', gap: 1 }}>
          <YBButton
            variant="secondary"
            size="large"
            onClick={onClose}
            dataTestId="unsupported-feature-warning-cancel"
          >
            {t('cancel')}
          </YBButton>
          <YBButton
            variant="primary"
            size="large"
            onClick={onSwitchBack}
            disabled={!isUniversesReady}
            sx={{
              backgroundColor: '#FF5F3B',
              '&:hover': { backgroundColor: '#E34E2C' }
            }}
            dataTestId="unsupported-feature-warning-switch-back"
          >
            {t('switchBack')}
          </YBButton>
        </Box>
      }
    >
      {!isUniversesReady ? (
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            minHeight: 280
          }}
        >
          <YBLoadingCircleIcon />
        </Box>
      ) : (
        <Box
          sx={{
            display: 'flex',
            flexDirection: 'column',
            gap: 3,
            padding: 2,
            border: '1px solid #E9EEF2',
            borderRadius: 1,
            color: '#0B1117'
          }}
        >
          <Box sx={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
            <Typography sx={{ fontSize: 13, fontWeight: 600, lineHeight: '18px', color: '#0B1117' }}>
              {t('affectedUniverses')}
            </Typography>
            <BulletList sx={{ maxHeight: 108, overflowY: 'auto' }}>
              {universeNames.map((universeName) => (
                <li key={universeName}>{universeName}</li>
              ))}
            </BulletList>
          </Box>

          <Typography sx={{ fontSize: 13, fontWeight: 400, lineHeight: '18px' }}>
            {t('accessChange')}{' '}
            <Box
              component="a"
              href={DEFAULT_RELEASE_NOTES_URL}
              target="_blank"
              rel="noopener noreferrer"
              sx={{ color: 'inherit', textDecoration: 'underline' }}
            >
              {t('learnMore')}
            </Box>
          </Typography>

          <Box
            sx={{
              display: 'flex',
              flexDirection: 'column',
              gap: 1,
              p: 2,
              border: '1px solid #E9EEF2',
              borderRadius: 1,
              backgroundColor: '#F7F9FB'
            }}
          >
            <Typography sx={{ fontSize: 13, fontWeight: 400, lineHeight: '18px' }}>
              {t('afterSwitchingBack')}
            </Typography>
            <BulletList>
              <li>{t('preferredAzRanking')}</li>
              <li>{t('databaseQueryLogging')}</li>
              <li>{t('proxies')}</li>
            </BulletList>
          </Box>
        </Box>
      )}
    </YBModal>
  );
};
