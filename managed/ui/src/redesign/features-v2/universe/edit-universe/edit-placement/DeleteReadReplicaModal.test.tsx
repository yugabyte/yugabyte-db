import type { ReactNode } from 'react';
import { render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import { QueryClient, QueryClientProvider } from 'react-query';
import { ThemeProvider } from '@material-ui/core';
import { Provider } from 'react-redux';
import { createStore } from 'redux';
import { describe, expect, it, vi, beforeEach, afterEach } from 'vitest';
import { mainTheme } from '@app/redesign/theme/mainTheme';
import { DeleteReadReplicaModal } from './DeleteReadReplicaModal';

vi.mock('@yugabyte-ui-library/core', async (importOriginal) => {
  const mod = await importOriginal<typeof import('@yugabyte-ui-library/core')>();
  const YBModalMock = ({
    children,
    open,
    onSubmit,
    buttonProps
  }: {
    children?: ReactNode;
    open: boolean;
    onSubmit?: () => void;
    buttonProps?: { primary?: { disabled?: boolean; dataTestId?: string; loading?: boolean } };
  }) => {
    if (!open) return null;
    const testId = buttonProps?.primary?.dataTestId ?? 'submit-delete-read-replica';
    return (
      <div data-testid="delete-read-replica-modal-wrapper">
        {children}
        <button
          type="button"
          data-testid={testId}
          disabled={Boolean(buttonProps?.primary?.disabled)}
          onClick={() => onSubmit?.()}
        >
          submit-mock
        </button>
      </div>
    );
  };
  return {
    ...mod,
    yba: {
      ...mod.yba,
      YBModal: YBModalMock
    }
  };
});

vi.mock('react-i18next', () => ({
  useTranslation: () => ({
    t: (key: string) => key,
    i18n: { changeLanguage: () => new Promise(() => {}) }
  }),
  Trans: ({ children }: { children?: React.ReactNode }) => children ?? null
}));

const hoisted = vi.hoisted(() => ({
  mutateAsync: vi.fn().mockResolvedValue({ task_uuid: 'task-rr-1' }),
  dispatch: vi.fn()
}));

vi.mock('@app/v2/api/universe/universe', async (importOriginal) => {
  const actual = await importOriginal<typeof import('@app/v2/api/universe/universe')>();
  return {
    ...actual,
    useDeleteCluster: () => ({
      mutateAsync: hoisted.mutateAsync,
      isLoading: false
    })
  };
});

vi.mock('react-redux', async (importOriginal) => {
  const actual = await importOriginal<typeof import('react-redux')>();
  return {
    ...actual,
    useDispatch: () => hoisted.dispatch
  };
});

const noopStore = createStore(() => ({}));

function renderModal(props: {
  open?: boolean;
  universeDisplayName?: string;
  universeUuid?: string;
  clusterUuid?: string;
}) {
  const queryClient = new QueryClient({
    defaultOptions: { queries: { retry: false }, mutations: { retry: false } }
  });
  return render(
    <ThemeProvider theme={mainTheme}>
      <Provider store={noopStore}>
        <QueryClientProvider client={queryClient}>
          <DeleteReadReplicaModal
            open={props.open ?? true}
            onClose={vi.fn()}
            universeUuid={props.universeUuid ?? 'uni-1'}
            clusterUuid={props.clusterUuid ?? 'cluster-rr-1'}
            universeDisplayName={props.universeDisplayName ?? 'my-universe'}
          />
        </QueryClientProvider>
      </Provider>
    </ThemeProvider>
  );
}

describe('DeleteReadReplicaModal', () => {
  beforeEach(() => {
    hoisted.mutateAsync.mockClear();
    hoisted.dispatch.mockClear();
    localStorage.setItem('customerId', 'customer-1');
  });

  afterEach(() => {
    localStorage.removeItem('customerId');
  });

  it('keeps submit disabled until universe name matches exactly', async () => {
    const user = userEvent.setup();
    renderModal({ universeDisplayName: 'Exact-Universe-Name' });
    const submit = screen.getByTestId('submit-delete-read-replica');
    expect(submit).toBeDisabled();

    const input = screen.getByTestId('validate-universename');
    await user.type(input, 'wrong');
    await waitFor(() => expect(submit).toBeDisabled());

    await user.clear(input);
    await user.type(input, 'Exact-Universe-Name');
    await waitFor(() => expect(submit).not.toBeDisabled());
  });

  it('calls delete mutation with universe and cluster ids after submit', async () => {
    const user = userEvent.setup();
    renderModal({
      universeDisplayName: 'Exact-Universe-Name',
      universeUuid: 'uni-99',
      clusterUuid: 'cls-88'
    });
    const input = screen.getByTestId('validate-universename');
    await user.type(input, 'Exact-Universe-Name');
    const submit = await screen.findByTestId('submit-delete-read-replica');
    await waitFor(() => expect(submit).not.toBeDisabled());
    await user.click(submit);

    await waitFor(() => {
      expect(hoisted.mutateAsync).toHaveBeenCalledWith(
        expect.objectContaining({
          uniUUID: 'uni-99',
          clsUUID: 'cls-88',
          cUUID: 'customer-1'
        }),
        expect.any(Object)
      );
    });
  });

  it('returns null when closed so modal content is not mounted', () => {
    renderModal({ open: false });
    expect(screen.queryByTestId('delete-read-replica-modal')).not.toBeInTheDocument();
  });
});
