import { FC, RefObject, useCallback, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';
import { TourStep, dismissTourStep, isTourStepDismissed } from '../tour-progress';
import { OnboardingTourPopper } from './OnboardingTourPopper';

const { Typography, styled } = mui;

const POPOVER_OFFSET: [number, number] = [0, 12];

/** Figma PLG/Purple 300 — primary CTA on this spotlight. */
const SEE_WHATS_CHANGED_BUTTON_BG = '#7879F1';

interface BeforeNewExperiencePopoverProps {
  open: boolean;
  anchorRef: RefObject<HTMLElement>;
  /** Permanent Hide Tip. */
  onClose: () => void;
  /** Transient click-away close. */
  onClickAway: () => void;
  onSeeWhatsChanged: () => void;
}

const GradientTitle = styled(Typography)(() => ({
  fontSize: 15,
  fontWeight: 600,
  lineHeight: '20px',
  backgroundImage:
    'linear-gradient(-56deg, #ED35EC 5.14%, #ED35C5 38.93%, #7879F1 75.17%, #5E60F0 98.9%)',
  WebkitBackgroundClip: 'text',
  backgroundClip: 'text',
  color: 'transparent'
}));

const WideSpotlight = styled(YBTourSpotlight)(() => ({
  '&&': {
    // Figma node 17648:65548
    width: 337,
    height: 184,
    minHeight: 184,
    maxWidth: 'calc(100vw - 32px)',
    boxSizing: 'border-box'
  },
  [`& [data-testid="before-new-experience-popover-spotlight-next"]`]: {
    backgroundColor: SEE_WHATS_CHANGED_BUTTON_BG,
    borderColor: SEE_WHATS_CHANGED_BUTTON_BG,
    '&:hover, &:focus': {
      backgroundColor: SEE_WHATS_CHANGED_BUTTON_BG,
      borderColor: SEE_WHATS_CHANGED_BUTTON_BG
    }
  }
}));

export const isBeforeNewExperiencePopoverDismissed = (): boolean =>
  isTourStepDismissed(TourStep.BeforeExp);

export const dismissBeforeNewExperiencePopover = (): void => {
  dismissTourStep(TourStep.BeforeExp);
};

export const useBeforeNewExperiencePopover = () => {
  const anchorRef = useRef<HTMLSpanElement>(null);
  const [open, setOpen] = useState(false);

  const openPopover = useCallback(() => {
    if (!isBeforeNewExperiencePopoverDismissed()) {
      setOpen(true);
    }
  }, []);

  const handleClose = useCallback(() => {
    dismissBeforeNewExperiencePopover();
    setOpen(false);
  }, []);

  return {
    open,
    setOpen,
    anchorRef,
    openPopover,
    handleClose,
    handleClickAway: handleClose
  };
};

export const BeforeNewExperiencePopover: FC<BeforeNewExperiencePopoverProps> = ({
  open,
  anchorRef,
  onClose,
  onClickAway,
  onSeeWhatsChanged
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.beforeNewExperiencePopover'
  });

  return (
    <OnboardingTourPopper
      open={open}
      anchorEl={anchorRef.current}
      placement={TourPlacement.BottomStart}
      offset={POPOVER_OFFSET}
      onClickAway={onClickAway}
    >
      <WideSpotlight
        title={<GradientTitle component="span">{t('title')}</GradientTitle>}
        body={t('body')}
        badgeLabel=""
        showNext
        nextLabel={t('seeWhatsChanged')}
        dismissLabel={t('hideTip')}
        placement={TourPlacement.BottomStart}
        dataTestId="before-new-experience-popover-spotlight"
        onNext={onSeeWhatsChanged}
        onDismiss={onClose}
      />
    </OnboardingTourPopper>
  );
};
