import { FC, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui } from '@yugabyte-ui-library/core';
import { WhereThingsMovedModal } from '../modals/WhereThingsMovedModal';
import LifeBuoyIcon from '@app/redesign/assets/where-things-moved/life-buoy.svg';

const { Box, styled } = mui;

/** Figma node 15921:86122 — Life-Buoy tip under Settings tabs. */
const TipPill = styled('button')(() => ({
  display: 'inline-flex',
  alignItems: 'center',
  justifyContent: 'center',
  gap: 2,
  height: 28,
  marginTop: 16,
  // Align left edge with Settings tabs (no extra left inset).
  marginLeft: 0,
  padding: '6px 10px 6px 8px',
  border: '1px solid #CBCCFB',
  borderRadius: 50,
  backgroundColor: '#F2F3FE',
  color: '#5542B9',
  fontFamily: 'Inter',
  fontSize: 11.5,
  fontWeight: 400,
  lineHeight: '16px',
  whiteSpace: 'nowrap',
  cursor: 'pointer',
  alignSelf: 'flex-start',
  '&:hover': {
    backgroundColor: '#E8E9FD'
  },
  '&:focus-visible': {
    outline: '2px solid #735AF5',
    outlineOffset: 2
  }
}));

/**
 * Pill CTA under Settings tabs that opens the "Where Things Moved" modal.
 */
export const WhereThingsMovedTip: FC = () => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.whereThingsMovedTip'
  });
  const [open, setOpen] = useState(false);

  return (
    <>
      <TipPill type="button" onClick={() => setOpen(true)} data-testid="where-things-moved-tip">
        <Box
          component="span"
          sx={{
            display: 'inline-flex',
            width: 18,
            height: 18,
            flexShrink: 0,
            alignItems: 'center',
            justifyContent: 'center',
            overflow: 'hidden',
            '& > svg': {
              width: 18,
              height: 18,
              display: 'block'
            }
          }}
        >
          <LifeBuoyIcon />
        </Box>
        {t('label')}
      </TipPill>
      <WhereThingsMovedModal open={open} onClose={() => setOpen(false)} />
    </>
  );
};
