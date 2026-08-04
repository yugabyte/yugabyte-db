import { FC, RefObject, useCallback, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';
import { requestOpenUniverseCreationPopover } from './UniverseCreationPopover';

const { Box, Link, Popper, Typography, styled } = mui;

const POPOVER_OFFSET: [number, number] = [0, 12];

export const AFTER_NEW_EXPERIENCE_POPOVER_DISMISS_KEY =
  'yb_after_new_experience_popover_dismissed';

interface AfterNewExperiencePopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  onClose: () => void;
  onSeeWhatsChanged: () => void;
}

const GradientTitle = styled(Typography)(() => ({
  fontSize: 15,
  fontWeight: 600,
  lineHeight: '20px',
  backgroundImage:
    'linear-gradient(-83deg, #ED35EC 5.14%, #ED35C5 38.93%, #7879F1 75.17%, #5E60F0 98.9%)',
  WebkitBackgroundClip: 'text',
  backgroundClip: 'text',
  color: 'transparent'
}));

const BodyText = styled(Typography)(({ theme }) => ({
  fontSize: 13,
  fontWeight: 400,
  lineHeight: '20px',
  color: theme.palette.grey[700],
  margin: 0
}));

const SeeWhatsChangedLink = styled(Link)(({ theme }) => ({
  display: 'inline-block',
  marginTop: 8,
  fontSize: 13,
  fontWeight: 400,
  lineHeight: '20px',
  color: theme.palette.primary[600],
  textDecoration: 'underline',
  cursor: 'pointer',
  '&:hover': {
    color: theme.palette.primary[600],
    textDecoration: 'underline'
  }
}));

const WideSpotlight = styled(YBTourSpotlight)(() => ({
  '&&': {
    width: 349,
    minHeight: 'unset',
    maxWidth: 'calc(100vw - 32px)'
  }
}));

export const isAfterNewExperiencePopoverDismissed = (): boolean =>
  localStorage.getItem(AFTER_NEW_EXPERIENCE_POPOVER_DISMISS_KEY) === 'true';

export const dismissAfterNewExperiencePopover = (): void => {
  localStorage.setItem(AFTER_NEW_EXPERIENCE_POPOVER_DISMISS_KEY, 'true');
};

export const useAfterNewExperiencePopover = () => {
  const anchorRef = useRef<HTMLSpanElement>(null);
  const [open, setOpen] = useState(false);

  const openPopover = useCallback(() => {
    if (!isAfterNewExperiencePopoverDismissed()) {
      setOpen(true);
    }
  }, []);

  const handleClose = useCallback(() => {
    dismissAfterNewExperiencePopover();
    setOpen(false);
    requestOpenUniverseCreationPopover();
  }, []);

  return {
    open,
    setOpen,
    anchorRef,
    openPopover,
    handleClose
  };
};

export const AfterNewExperiencePopover: FC<AfterNewExperiencePopoverProps> = ({
  open,
  anchorRef,
  onClose,
  onSeeWhatsChanged
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.afterNewExperiencePopover'
  });

  const body = (
    <Box>
      <BodyText>{t('bodyLine1')}</BodyText>
      <BodyText sx={{ mt: 1 }}>{t('bodyLine2')}</BodyText>
      <SeeWhatsChangedLink
        component="button"
        type="button"
        onClick={onSeeWhatsChanged}
        data-testid="after-new-experience-see-whats-changed"
      >
        {t('seeWhatsChanged')}
      </SeeWhatsChangedLink>
    </Box>
  );

  return (
    <Popper
      open={open}
      anchorEl={anchorRef.current}
      placement={TourPlacement.BottomStart}
      modifiers={[
        {
          name: 'offset',
          options: {
            offset: POPOVER_OFFSET
          }
        }
      ]}
      sx={{ zIndex: 2200 }}
    >
      <WideSpotlight
        title={<GradientTitle component="span">{t('title')}</GradientTitle>}
        body={body}
        badgeLabel=""
        showNext={false}
        dismissLabel={t('hideTip')}
        placement={TourPlacement.BottomStart}
        dataTestId="after-new-experience-popover-spotlight"
        onDismiss={onClose}
      />
    </Popper>
  );
};
