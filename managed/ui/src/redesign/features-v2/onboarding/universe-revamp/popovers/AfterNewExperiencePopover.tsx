import { FC, RefObject, useCallback, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { mui, TourPlacement, YBTourSpotlight } from '@yugabyte-ui-library/core';
import { TourStep, dismissTourStep, isTourStepDismissed } from '../tour-progress';
import { requestOpenUniverseCreationPopover } from './UniverseCreationPopover';
import { OnboardingTourPopper } from './OnboardingTourPopper';

const { Typography, styled } = mui;

const POPOVER_OFFSET: [number, number] = [0, 12];

/** Figma PLG/Purple 300 — primary CTA on this spotlight. */
const SEE_WHATS_CHANGED_BUTTON_BG = '#7879F1';

interface AfterNewExperiencePopoverProps {
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
    'linear-gradient(-62deg, #ED35EC 5.14%, #ED35C5 38.93%, #7879F1 75.17%, #5E60F0 98.9%)',
  WebkitBackgroundClip: 'text',
  backgroundClip: 'text',
  color: 'transparent'
}));

const WideSpotlight = styled(YBTourSpotlight)(() => ({
  '&&': {
    // Figma node 15962:122831 / 17648:65802
    width: 349,
    height: 204,
    minHeight: 204,
    maxWidth: 'calc(100vw - 32px)',
    boxSizing: 'border-box'
  },
  [`& [data-testid="after-new-experience-popover-spotlight-next"]`]: {
    backgroundColor: SEE_WHATS_CHANGED_BUTTON_BG,
    borderColor: SEE_WHATS_CHANGED_BUTTON_BG,
    '&:hover, &:focus': {
      backgroundColor: SEE_WHATS_CHANGED_BUTTON_BG,
      borderColor: SEE_WHATS_CHANGED_BUTTON_BG
    }
  }
}));

export const isAfterNewExperiencePopoverDismissed = (): boolean =>
  isTourStepDismissed(TourStep.AfterExp);

export const dismissAfterNewExperiencePopover = (): void => {
  dismissTourStep(TourStep.AfterExp);
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
    handleClose,
    handleClickAway: handleClose
  };
};

export const AfterNewExperiencePopover: FC<AfterNewExperiencePopoverProps> = ({
  open,
  anchorRef,
  onClose,
  onClickAway,
  onSeeWhatsChanged
}) => {
  const { t } = useTranslation('translation', {
    keyPrefix: 'onBoarding.afterNewExperiencePopover'
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
        dataTestId="after-new-experience-popover-spotlight"
        onNext={onSeeWhatsChanged}
        onDismiss={onClose}
      />
    </OnboardingTourPopper>
  );
};
