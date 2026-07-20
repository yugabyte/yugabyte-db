import React from 'react';
import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';
import { YBThemeProvider, coreTheme } from '@yugabyte-ui-library/core';
import { ResizeUpdateOption } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import {
  ReviewHardwareChangesModal,
  type HardwareReviewSection
} from './ReviewHardwareChangesModal';

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { changeLanguage: () => new Promise(() => {}) }
  }),
  Trans: ({ i18nKey }: { i18nKey?: string; children?: React.ReactNode }) => (
    <span>{i18nKey ?? null}</span>
  )
}));

vi.mock('@yugabyte-ui-library/core', async (importOriginal) => {
  const actual = await importOriginal<typeof import('@yugabyte-ui-library/core')>();
  return {
    ...actual,
    YBAlert: ({
      text,
      variant
    }: {
      text: React.ReactNode;
      variant?: string;
    }) => (
      <div data-testid="hardware-alert" data-variant={variant}>
        {text}
      </div>
    ),
    YBRadio: ({
      checked,
      disabled,
      onChange,
      dataTestId,
      value
    }: {
      checked?: boolean;
      disabled?: boolean;
      onChange?: () => void;
      dataTestId: string;
      value: string;
    }) => (
      <input
        type="radio"
        data-testid={dataTestId}
        value={value}
        checked={!!checked}
        disabled={!!disabled}
        onChange={() => onChange?.()}
      />
    ),
    YBInput: ({
      value,
      onChange,
      dataTestId,
      inputProps
    }: {
      value?: string;
      onChange?: (e: React.ChangeEvent<HTMLInputElement>) => void;
      dataTestId?: string;
      inputProps?: { 'data-testid'?: string };
    }) => (
      <input
        data-testid={dataTestId ?? inputProps?.['data-testid']}
        value={value}
        onChange={onChange}
      />
    ),
    yba: {
      ...actual.yba,
      YBModal: ({
        open,
        children,
        onSubmit,
        submitLabel,
        buttonProps
      }: {
        open: boolean;
        children: React.ReactNode;
        onSubmit?: () => void;
        submitLabel?: string;
        buttonProps?: { primary?: { disabled?: boolean; dataTestId?: string } };
      }) =>
        open ? (
          <div>
            {children}
            <button
              data-testid={buttonProps?.primary?.dataTestId ?? 'submit'}
              disabled={buttonProps?.primary?.disabled}
              onClick={onSubmit}
            >
              {submitLabel}
            </button>
          </div>
        ) : null
    }
  };
});

const sections: HardwareReviewSection[] = [
  {
    current: {
      instanceType: 'c5.xlarge',
      instanceTypeLabel: 'c5.xlarge',
      volumeSize: 100,
      numVolumes: 1
    },
    next: {
      instanceType: 'c5.2xlarge',
      instanceTypeLabel: 'c5.2xlarge',
      volumeSize: 200,
      numVolumes: 1
    }
  }
];

function renderReview(
  props: Partial<React.ComponentProps<typeof ReviewHardwareChangesModal>> = {}
) {
  const onConfirm = vi.fn();
  const onClose = vi.fn();
  render(
    <YBThemeProvider theme={coreTheme}>
      <ReviewHardwareChangesModal
        visible
        sections={sections}
        onClose={onClose}
        onConfirm={onConfirm}
        resizeOptions={[ResizeUpdateOption.SMART_RESIZE, ResizeUpdateOption.FULL_MOVE]}
        replicationFactor={3}
        {...props}
      />
    </YBThemeProvider>
  );
  return { onConfirm, onClose };
}

describe('ReviewHardwareChangesModal', () => {
  it('enables both radios and defaults to rolling when both options are available', () => {
    const { onConfirm } = renderReview();

    expect(screen.getByTestId('hardware-rolling-restart')).not.toBeDisabled();
    expect(screen.getByTestId('hardware-migrate-nodes')).not.toBeDisabled();
    expect(screen.getByTestId('hardware-rolling-restart')).toBeChecked();
    expect(screen.getByTestId('hardware-delay-seconds-input')).toBeInTheDocument();

    fireEvent.click(screen.getByTestId('edit-hardware-confirm-and-apply'));
    expect(onConfirm).toHaveBeenCalledWith(
      expect.objectContaining({ strategy: 'rolling', delaySeconds: 240 })
    );
  });

  it('shows full-move info and hides options when only FULL_MOVE is available', () => {
    const { onConfirm } = renderReview({
      resizeOptions: [ResizeUpdateOption.FULL_MOVE]
    });

    expect(screen.getByTestId('hardware-alert')).toHaveAttribute('data-variant', 'info');
    expect(screen.getByText('fullMoveInfo')).toBeInTheDocument();
    expect(screen.queryByTestId('hardware-rolling-restart')).not.toBeInTheDocument();
    expect(screen.queryByTestId('hardware-migrate-nodes')).not.toBeInTheDocument();

    fireEvent.click(screen.getByTestId('edit-hardware-confirm-and-apply'));
    expect(onConfirm).toHaveBeenCalledWith(expect.objectContaining({ strategy: 'migrate' }));
  });

  it('keeps options when only SMART_RESIZE is available and shows delay', () => {
    renderReview({
      resizeOptions: [ResizeUpdateOption.SMART_RESIZE]
    });

    expect(screen.getByTestId('hardware-migrate-nodes')).toBeDisabled();
    expect(screen.getByTestId('hardware-rolling-restart')).not.toBeDisabled();
    expect(screen.getByTestId('hardware-delay-seconds-input')).toBeInTheDocument();
  });

  it('shows non-restart info and hides options for SMART_RESIZE_NON_RESTART only', () => {
    const { onConfirm } = renderReview({
      resizeOptions: [ResizeUpdateOption.SMART_RESIZE_NON_RESTART]
    });

    expect(screen.getByTestId('hardware-alert')).toHaveAttribute('data-variant', 'info');
    expect(screen.getByText('nonRestartInfo')).toBeInTheDocument();
    expect(screen.queryByTestId('hardware-rolling-restart')).not.toBeInTheDocument();
    expect(screen.queryByTestId('hardware-delay-seconds-input')).not.toBeInTheDocument();

    fireEvent.click(screen.getByTestId('edit-hardware-confirm-and-apply'));
    expect(onConfirm).toHaveBeenCalledWith(expect.objectContaining({ strategy: 'rolling' }));
  });

  it('shows RF1 warning instead of online description when RF is 1', () => {
    renderReview({ replicationFactor: 1 });

    expect(screen.getByTestId('hardware-alert')).toHaveAttribute('data-variant', 'error');
    expect(screen.getByText('rollingRestartRf1Warning')).toBeInTheDocument();
    expect(screen.getByText('restartNodes')).toBeInTheDocument();
    expect(screen.queryByText('rollingRestartDescription')).not.toBeInTheDocument();
    expect(screen.queryByTestId('hardware-delay-seconds-input')).not.toBeInTheDocument();
  });

  it('shows online description for RF >= 3', () => {
    renderReview({ replicationFactor: 3 });

    expect(screen.queryByTestId('hardware-alert')).not.toBeInTheDocument();
    expect(screen.getByText('rollingRestartDescription')).toBeInTheDocument();
  });

  it('disables confirm while options are loading', () => {
    renderReview({
      resizeOptions: undefined,
      isLoadingOptions: true
    });

    expect(screen.getByTestId('edit-hardware-confirm-and-apply')).toBeDisabled();
    expect(screen.getByTestId('hardware-rolling-restart')).toBeDisabled();
    expect(screen.getByTestId('hardware-migrate-nodes')).toBeDisabled();
  });

  it('disables confirm when no options are available', () => {
    renderReview({
      resizeOptions: [],
      isLoadingOptions: false
    });

    expect(screen.getByTestId('edit-hardware-confirm-and-apply')).toBeDisabled();
  });

  it('passes migrate strategy when user selects migrate', () => {
    const { onConfirm } = renderReview();

    fireEvent.click(screen.getByTestId('hardware-migrate-nodes'));
    fireEvent.click(screen.getByTestId('edit-hardware-confirm-and-apply'));

    expect(onConfirm).toHaveBeenCalledWith(expect.objectContaining({ strategy: 'migrate' }));
  });
});
