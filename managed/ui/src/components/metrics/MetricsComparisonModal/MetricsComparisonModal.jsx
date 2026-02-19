import { YBModal } from '../../common/forms/fields';
import './MetricsComparisonModal.scss';
import { NodeSelectorHeader } from './NodeSelectorHeader';
import { MetricsFilterHeader } from './MetricsFilterHeader';
import { ComparisonFilterContextProvider } from './ComparisonFilterContextProvider';
import { MetricsComparisonContent } from './MetricsComparisonContent';

export const MetricsComparisonModal = ({
  selectedUniverse,
  visible,
  onHide,
  origin,
  selectedRegionClusterUUID
}) => {
  return (
    <ComparisonFilterContextProvider selectedUniverse={selectedUniverse}>
      <div className="metrics-comparison-modal">
        <YBModal
          visible={visible}
          onHide={onHide}
          className="metrics-comparison-modal"
          cancelLabel="Cancel"
          showCancelButton={true}
          submitLabel="Close"
          title={selectedUniverse.name}
          dialogClassName="modal-90w"
          customHeader={<MetricsFilterHeader selectedUniverse={selectedUniverse} origin={origin} />}
          formClassName="metrics-comparison-form"
        >
          <NodeSelectorHeader
            universe={selectedUniverse}
            selectedRegionClusterUUID={selectedRegionClusterUUID}
          />
          <MetricsComparisonContent universe={selectedUniverse} visible={visible} />
        </YBModal>
      </div>
    </ComparisonFilterContextProvider>
  );
};
