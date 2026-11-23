import { FormProvider, useForm } from 'react-hook-form';
import { act, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { AvailabilityZoneField } from './AvailabilityZoneField';
import { REGIONS_FIELD, SINGLE_AVAILABILITY_ZONE } from '../FieldNames';

const regionA = { code: 'a', name: 'A', zones: [{ code: 'a1', name: 'a1' }] };
const regionB = { code: 'b', name: 'B', zones: [{ code: 'b1', name: 'b1' }] };

let methods: ReturnType<typeof useForm<any>>;

const Harness = ({ az }: { az: string }) => {
  methods = useForm<any>({
    defaultValues: { [REGIONS_FIELD]: [regionA], [SINGLE_AVAILABILITY_ZONE]: az },
    resolver: (values) =>
      values[SINGLE_AVAILABILITY_ZONE]
        ? { values, errors: {} }
        : { values: {}, errors: { [SINGLE_AVAILABILITY_ZONE]: { type: 'required', message: 'req' } } }
  });
  return (
    <FormProvider {...methods}>
      <AvailabilityZoneField name={SINGLE_AVAILABILITY_ZONE} label="az" dataTestId="az" />
    </FormProvider>
  );
};

describe('AvailabilityZoneField', () => {
  it('clears the AZ when the region changes to one without it', () => {
    render(<Harness az="a1" />);
    expect(methods.getValues(SINGLE_AVAILABILITY_ZONE)).toBe('a1');
    act(() => methods.setValue(REGIONS_FIELD, [regionB]));
    expect(methods.getValues(SINGLE_AVAILABILITY_ZONE)).toBe('');
  });

  it('keeps the AZ when it still belongs to the selected region', () => {
    render(<Harness az="a1" />);
    act(() => methods.setValue(REGIONS_FIELD, [{ ...regionA }]));
    expect(methods.getValues(SINGLE_AVAILABILITY_ZONE)).toBe('a1');
  });

  it('clearing the autocomplete does not throw', () => {
    render(<Harness az="a1" />);
    const input = screen.getByTestId('availability-zone-field').querySelector('input')!;
    expect(() => act(() => methods.setValue(SINGLE_AVAILABILITY_ZONE, ''))).not.toThrow();
    expect(input).toBeTruthy();
  });

  it('revalidates so the required error goes away once an AZ is chosen', async () => {
    render(<Harness az="a1" />);
    act(() => methods.setValue(REGIONS_FIELD, [regionB]));
    await waitFor(() => expect(methods.formState.errors[SINGLE_AVAILABILITY_ZONE]).toBeTruthy());
    const input = screen.getByTestId('availability-zone-field').querySelector('input')!;
    fireEvent.mouseDown(input);
    fireEvent.click(await screen.findByText('b1'));
    await waitFor(() => expect(methods.formState.errors[SINGLE_AVAILABILITY_ZONE]).toBeUndefined());
  });
});
