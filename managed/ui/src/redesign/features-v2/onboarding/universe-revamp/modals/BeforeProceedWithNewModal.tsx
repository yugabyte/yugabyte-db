import { FC, useCallback } from 'react';
import { Trans, useTranslation } from 'react-i18next';
import { YBButton, YBModal, mui } from '@yugabyte-ui-library/core';

import BulletIcon from '@app/redesign/assets/what-changed/bullet.svg';
import NavArrowIcon from '@app/redesign/assets/what-changed/nav-arrow.svg';
import { requestOpenDetailSettingsPopover } from '../popovers/DetailSettingsPopover';
import {
  ArrowWrap,
  BulletWrap,
  FooterActions,
  NoteCard,
  PathMappingLabel,
  PathMappingRow
} from './HelperComponent';

const { Box, Typography } = mui;

interface BeforeProceedWithNewModalProps {
  open: boolean;
  onClose: () => void;
}

interface PathMapping {
  thenKey: string;
  nowPathKey: string;
}

const PATH_MAPPINGS: PathMapping[] = [
  {
    thenKey: 'databaseAuditLogs',
    nowPathKey: 'nowLogs'
  },
  {
    thenKey: 'databaseAuditLogExport',
    nowPathKey: 'nowTelemetryExport'
  }
];

export const BeforeProceedWithNewModal: FC<BeforeProceedWithNewModalProps> = ({
  open,
  onClose
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.beforeProceedWithNewModal'
  });

  const handleClose = useCallback(() => {
    onClose();
    // Defer so the close click does not immediately click-away the Settings tip.
    window.setTimeout(() => requestOpenDetailSettingsPopover(), 0);
  }, [onClose]);

  return (
    <YBModal
      open={open}
      onClose={handleClose}
      title={t('title')}
      titleSeparator
      size="md"
      overrideWidth={600}
      overrideHeight="auto"
      hideCloseBtn={false}
      // Above OnBoardingBanner (2100) and HighlightedStatsPanel (2040).
      sx={{ zIndex: 2300 }}
      // yba YBModal only mounts actions when submit/cancel labels are set;
      // customButtonTemplate then replaces the default submit control.
      submitLabel={t('close')}
      dialogContentProps={{
        dividers: false,
        sx: {
          padding: '16px !important',
          backgroundColor: '#FFFFFF',
          display: 'flex',
          flexDirection: 'column',
          gap: '16px'
        }
      }}
      customButtonTemplate={
        <FooterActions>
          <YBButton
            variant="secondary"
            size="large"
            onClick={handleClose}
            dataTestId="before-proceed-with-new-modal-close"
          >
            {t('close')}
          </YBButton>
        </FooterActions>
      }
    >
      <Typography
        sx={{
          fontSize: '13px',
          fontWeight: 400,
          lineHeight: '26px',
          color: 'grey.900'
        }}
      >
        <Trans
          t={t}
          i18nKey="note"
          components={{
            settings: <Box component="span" sx={{ color: 'grey.900', fontWeight: 600 }} />
          }}
        />
      </Typography>

      <NoteCard>
        {PATH_MAPPINGS.map((row) => (
          <PathMappingRow key={row.thenKey}>
            <PathMappingLabel>
              <BulletWrap>
                <BulletIcon />
              </BulletWrap>
              <Typography
                sx={{
                  fontSize: '13px',
                  fontWeight: 400,
                  lineHeight: '18px',
                  color: 'grey.900',
                  whiteSpace: 'nowrap'
                }}
              >
                {t(row.thenKey)}
              </Typography>
            </PathMappingLabel>
            <ArrowWrap>
              <NavArrowIcon />
            </ArrowWrap>
            <Typography
              sx={{
                fontSize: '13px',
                fontWeight: 400,
                lineHeight: '18px',
                color: 'grey.900',
                whiteSpace: 'nowrap'
              }}
            >
              <Box component="span" sx={{ fontWeight: 600 }}>
                {t('settings')}
              </Box>
              <Box component="span" sx={{ color: 'grey.700' }}>
                {'  '}
              </Box>
              {'/  '}
              <Box component="span" sx={{ color: '#735AF5', fontWeight: 600 }}>
                {t(row.nowPathKey)}
              </Box>
            </Typography>
          </PathMappingRow>
        ))}
      </NoteCard>
    </YBModal>
  );
};
