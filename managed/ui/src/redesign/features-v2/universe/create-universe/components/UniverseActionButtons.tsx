import { FC } from 'react';
import { yba, mui } from '@yugabyte-ui-library/core';
import { useTranslation } from 'react-i18next';

const { YBButton } = yba;

const { Grid2: Grid } = mui;

interface UniverseActionButtonsProps {
  cancelButton?: {
    text?: string;
    onClick: () => void;
    disabled?: boolean;
  };
  nextButton?: {
    text?: string;
    onClick: () => void;
    disabled?: boolean;
  };
  prevButton?: {
    text?: string;
    onClick: () => void;
    disabled?: boolean;
  };
  additionalButtons?: React.ReactElement;
}

export const UniverseActionButtons: FC<UniverseActionButtonsProps> = ({
  cancelButton,
  nextButton,
  prevButton,
  additionalButtons
}) => {
  const { t } = useTranslation('translation', { keyPrefix: 'common' });

  return (
    <Grid container alignItems="center" justifyContent="space-between" direction="row">
      <YBButton
        variant="secondary"
        size="large"
        dataTestId="create-universe-cancel-button"
        onClick={cancelButton?.onClick}
      >
        {cancelButton?.text ?? t('cancel')}
      </YBButton>
      <Grid container alignItems="center" justifyContent="flex-end" spacing={2}>
        {prevButton && (
          <YBButton
            onClick={() => {
              prevButton?.onClick();
            }}
            disabled={prevButton?.disabled}
            variant="secondary"
            size="large"
            dataTestId="create-universe-back-button"
          >
            {prevButton?.text ?? t('back')}
          </YBButton>
        )}
        {additionalButtons}
        <YBButton
          onClick={() => {
            nextButton?.onClick();
          }}
          variant="ybaPrimary"
          size="large"
          dataTestId="create-universe-next-button"
        >
          {nextButton?.text}
        </YBButton>
      </Grid>
    </Grid>
  );
};
