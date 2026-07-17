import { useEffect } from 'react';
import { render, act } from '@testing-library/react';
import { useForm } from 'react-hook-form';
import {
  resetProviderDependentSettings,
  usePersistStepFormValues
} from './persistStepFormValues';
import { initialCreateUniverseFormState } from '../CreateUniverseContext';

type FormValues = { name: string };

function PersistHarness({
  save,
  onReady
}: {
  save: (data: FormValues) => void;
  onReady: (api: { setName: (name: string) => void }) => void;
}) {
  const methods = useForm<FormValues>({ defaultValues: { name: 'initial' } });
  usePersistStepFormValues(methods.watch, methods.getValues, save);

  useEffect(() => {
    onReady({
      setName: (name: string) => methods.setValue('name', name)
    });
  }, [methods, onReady]);

  return null;
}

describe('usePersistStepFormValues', () => {
  it('persists form values when a field changes', () => {
    const save = vi.fn();
    let api: { setName: (name: string) => void } | undefined;

    render(
      <PersistHarness
        save={save}
        onReady={(ready) => {
          api = ready;
        }}
      />
    );

    save.mockClear();
    act(() => {
      api!.setName('universe-a');
    });

    expect(save).toHaveBeenCalled();
    expect(save.mock.calls[save.mock.calls.length - 1][0]).toMatchObject({
      name: 'universe-a'
    });
  });

  it('flushes latest values on unmount', () => {
    const save = vi.fn();
    let api: { setName: (name: string) => void } | undefined;

    const { unmount } = render(
      <PersistHarness
        save={save}
        onReady={(ready) => {
          api = ready;
        }}
      />
    );

    act(() => {
      api!.setName('before-unmount');
    });
    save.mockClear();
    unmount();

    expect(save).toHaveBeenCalledWith(
      expect.objectContaining({ name: 'before-unmount' })
    );
  });
});

describe('resetProviderDependentSettings', () => {
  it('resets only resilience, nodes, and instance slices', () => {
    const saveResilienceAndRegionsSettings = vi.fn();
    const saveNodesAvailabilitySettings = vi.fn();
    const saveInstanceSettings = vi.fn();

    resetProviderDependentSettings({
      saveResilienceAndRegionsSettings,
      saveNodesAvailabilitySettings,
      saveInstanceSettings
    });

    expect(saveResilienceAndRegionsSettings).toHaveBeenCalledWith(
      initialCreateUniverseFormState.resilienceAndRegionsSettings
    );
    expect(saveNodesAvailabilitySettings).toHaveBeenCalledWith(
      initialCreateUniverseFormState.nodesAvailabilitySettings
    );
    expect(saveInstanceSettings).toHaveBeenCalledWith(
      initialCreateUniverseFormState.instanceSettings
    );
  });
});
