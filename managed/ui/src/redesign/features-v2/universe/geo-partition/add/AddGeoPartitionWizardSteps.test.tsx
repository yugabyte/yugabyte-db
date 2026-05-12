import { render, screen } from '@testing-library/react';
import { describe, expect, it } from 'vitest';
import {
  AddGeoPartitionContextProps,
  initialAddGeoPartitionFormState
} from './AddGeoPartitionContext';
import { useGetSteps } from './AddGeoPartitionUtils';

function StepsDump({ ctx }: { ctx: AddGeoPartitionContextProps }) {
  const steps = useGetSteps(ctx);
  return (
    <div>
      {steps.map((s, groupIndex) => (
        <div key={groupIndex} data-testid={`group-${groupIndex}`}>
          <span data-testid={`group-title-${groupIndex}`}>{s.groupTitle}</span>
          {s.subSteps.map((sub, subIndex) => (
            <span key={subIndex} data-testid={`sub-${groupIndex}-${subIndex}`}>
              {sub.title}
            </span>
          ))}
        </div>
      ))}
    </div>
  );
}

describe('useGetSteps (geo partition wizard)', () => {
  it('when isNewGeoPartition and one partition: first group has only General Settings; then Review', () => {
    const ctx: AddGeoPartitionContextProps = {
      ...initialAddGeoPartitionFormState,
      isNewGeoPartition: true,
      geoPartitions: [{ ...initialAddGeoPartitionFormState.geoPartitions[0], name: 'Default Row' }]
    };
    render(<StepsDump ctx={ctx} />);
    expect(screen.getByTestId('group-0')).toBeInTheDocument();
    expect(screen.getByTestId('sub-0-0')).toHaveTextContent('General Settings');
    expect(screen.queryByTestId('sub-0-1')).not.toBeInTheDocument();
    expect(screen.getByTestId('group-title-1')).toHaveTextContent('Review');
    expect(screen.getByTestId('sub-1-0')).toHaveTextContent('Summary and Cost');
  });

  it('when isNewGeoPartition and two partitions: first group General only; second has full placement sub-steps', () => {
    const base = initialAddGeoPartitionFormState.geoPartitions[0];
    const ctx: AddGeoPartitionContextProps = {
      ...initialAddGeoPartitionFormState,
      isNewGeoPartition: true,
      geoPartitions: [
        { ...base, name: 'Primary default' },
        { ...base, name: 'Geo Partition 2', tablespaceName: 'Tablespace_2' }
      ]
    };
    render(<StepsDump ctx={ctx} />);
    expect(screen.getByTestId('sub-0-0')).toHaveTextContent('General Settings');
    expect(screen.queryByTestId('sub-0-1')).not.toBeInTheDocument();

    expect(screen.getByTestId('sub-1-0')).toHaveTextContent('General Settings');
    expect(screen.getByTestId('sub-1-1')).toHaveTextContent('Regions');
    expect(screen.getByTestId('sub-1-2')).toHaveTextContent('Availability Zones and Nodes');

    expect(screen.getByTestId('group-title-2')).toHaveTextContent('Review');
  });

  it('when not isNewGeoPartition and one partition: first group has all placement sub-steps', () => {
    const ctx: AddGeoPartitionContextProps = {
      ...initialAddGeoPartitionFormState,
      isNewGeoPartition: false,
      geoPartitions: [{ ...initialAddGeoPartitionFormState.geoPartitions[0], name: 'New Geo' }]
    };
    render(<StepsDump ctx={ctx} />);
    expect(screen.getByTestId('sub-0-0')).toHaveTextContent('General Settings');
    expect(screen.getByTestId('sub-0-1')).toHaveTextContent('Regions');
    expect(screen.getByTestId('sub-0-2')).toHaveTextContent('Availability Zones and Nodes');
    expect(screen.getByTestId('group-title-1')).toHaveTextContent('Review');
  });
});
