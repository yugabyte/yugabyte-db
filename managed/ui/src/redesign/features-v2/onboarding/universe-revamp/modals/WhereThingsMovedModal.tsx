import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { YBButton, YBModal, mui } from '@yugabyte-ui-library/core';

import NavArrowIcon from '@app/redesign/assets/what-changed/nav-arrow.svg';
import SortIcon from '@app/redesign/assets/what-changed/sort.svg';
import {
  ArrowWrap,
  NEW_EXPERIENCE_DOCS_URL,
  FooterActions,
  HeaderLabel,
  ModalBody,
  RelocationTable,
  SectionCard,
  TableHeader,
  TableRow
} from './HelperComponent';

const { Box, Typography } = mui;

interface WhereThingsMovedModalProps {
  open: boolean;
  onClose: () => void;
  onFindOutMore?: () => void;
}

interface RelocationRow {
  thenKey: string;
  nowPathKey: string;
  nowDetailKey?: string;
}

const RELOCATION_ROWS: RelocationRow[] = [
  {
    thenKey: 'editUniversePlacement',
    nowPathKey: 'nowPlacement',
    nowDetailKey: 'nowPlacementDetail'
  },
  {
    thenKey: 'editUniverseInstance',
    nowPathKey: 'nowHardware',
    nowDetailKey: 'nowHardwareDetail'
  },
  {
    thenKey: 'readReplica',
    nowPathKey: 'nowPlacement'
  },
  {
    thenKey: 'logs',
    nowPathKey: 'nowLogs'
  },
  {
    thenKey: 'exportLogs',
    nowPathKey: 'nowTelemetryExport'
  },
  {
    thenKey: 'exportMetrics',
    nowPathKey: 'nowTelemetryExport'
  }
];

export const WhereThingsMovedModal: FC<WhereThingsMovedModalProps> = ({
  open,
  onClose,
  onFindOutMore
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.whereThingsMovedModal'
  });

  const handleFindOutMore = () => {
    if (onFindOutMore) {
      onFindOutMore();
      return;
    }
    window.open(NEW_EXPERIENCE_DOCS_URL, '_blank', 'noopener,noreferrer');
    onClose();
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('title')}
      titleSeparator
      size="xl"
      overrideWidth={'auto'}
      overrideHeight="auto"
      hideCloseBtn={false}
      // Above OnBoardingBanner (2100) and HighlightedStatsPanel (2040).
      sx={{ zIndex: 2300 }}
      // yba YBModal only mounts actions when submit/cancel labels are set;
      // customButtonTemplate then replaces the default submit control.
      submitLabel={t('findOutMore')}
      dialogContentProps={{
        dividers: false,
        sx: {
          padding: '0 !important',
          backgroundColor: '#FBFCFD'
        }
      }}
      customButtonTemplate={
        <FooterActions>
          <YBButton
            variant="secondary"
            size="large"
            onClick={onClose}
            dataTestId="where-things-moved-modal-close"
          >
            {t('close')}
          </YBButton>
          <YBButton
            variant="gradient"
            size="large"
            onClick={handleFindOutMore}
            dataTestId="where-things-moved-modal-find-out-more"
          >
            {t('findOutMore')}
          </YBButton>
        </FooterActions>
      }
    >
      <ModalBody>
        <SectionCard>
          <Typography
            sx={{
              fontSize: '13px',
              fontWeight: 400,
              lineHeight: '16px',
              color: 'grey.700'
            }}
          >
            {t('description')}
          </Typography>

          <RelocationTable>
            <TableHeader>
              <HeaderLabel>
                {t('then')}
                <SortIcon />
              </HeaderLabel>
              <Box />
              <HeaderLabel>
                {t('now')}
                <SortIcon />
              </HeaderLabel>
            </TableHeader>
            {RELOCATION_ROWS.map((row) => (
              <TableRow key={`${row.thenKey}-${row.nowPathKey}-${row.nowDetailKey ?? ''}`}>
                <Typography
                  sx={{
                    fontSize: '13px',
                    fontWeight: 400,
                    lineHeight: '32px',
                    color: 'grey.900',
                    whiteSpace: 'nowrap'
                  }}
                >
                  {t(row.thenKey)}
                </Typography>
                <ArrowWrap>
                  <NavArrowIcon />
                </ArrowWrap>
                <Typography
                  sx={{
                    fontSize: '13px',
                    fontWeight: 400,
                    lineHeight: '32px',
                    color: 'grey.900',
                    whiteSpace: 'nowrap'
                  }}
                >
                  <Box component="span" sx={{ color: '#6D7C88' }}>
                    {t('settings')}
                  </Box>
                  {`  /  ${t(row.nowPathKey)}`}
                  {row.nowDetailKey ? (
                    <Box component="span" sx={{ color: 'grey.900', fontSize: '11.5px', ml: '4px' }}>
                      {t(row.nowDetailKey)}
                    </Box>
                  ) : null}
                </Typography>
              </TableRow>
            ))}
          </RelocationTable>
        </SectionCard>
      </ModalBody>
    </YBModal>
  );
};
