import { Col } from 'react-bootstrap';
import { Link } from 'react-router';

import { DescriptionItem, YBCost } from '../../common/descriptors';
import { UniverseStatusContainer } from '../../universes';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';
import {
  getClusterProviderUUIDs,
  getPrimaryCluster,
  getProviderMetadata,
  getUniverseNodeCount,
  optimizeVersion
} from '../../../utils/UniverseUtils';
import { ybFormatDate, YBTimeFormats } from '../../../redesign/helpers/DateUtils';
import { Universe } from '../../../redesign/features/universe/universe-form/utils/dto';

interface UniverseCardProps {
  universe: Universe;
  providers: any;
  refreshUniverseData: any;
  runtimeConfigs: any;
}

export const UniverseCard = ({
  universe,
  providers,
  refreshUniverseData,
  runtimeConfigs
}: UniverseCardProps) => {
  if (!isNonEmptyObject(universe)) {
    return <span />;
  }
  const primaryCluster = getPrimaryCluster(universe.universeDetails.clusters);
  if (!isNonEmptyObject(primaryCluster) || !isNonEmptyObject(primaryCluster.userIntent)) {
    return <span />;
  }
  const clusterProviderUUIDs = getClusterProviderUUIDs(universe.universeDetails.clusters);
  const clusterProviders = providers.data.filter((p: any) => clusterProviderUUIDs.includes(p.uuid));
  const replicationFactor = <span>{`${primaryCluster.userIntent.replicationFactor}`}</span>;
  const universeProviders = clusterProviders.map((provider: any) => {
    return getProviderMetadata(provider)?.name;
  });
  const universeProviderText = universeProviders.join(', ');

  const nodeCount = getUniverseNodeCount(universe.universeDetails.nodeDetailsSet);
  const isPricingKnown = universe.resources?.pricingKnown;
  const pricePerHour = universe.pricePerHour;
  const numNodes = <span>{nodeCount}</span>;
  let costPerMonth = <span>n/a</span>;

  if (isFinite(pricePerHour)) {
    costPerMonth = (
      <YBCost
        value={pricePerHour}
        multiplier={'month'}
        isPricingKnown={isPricingKnown}
        runtimeConfigs={runtimeConfigs}
      />
    );
  }
  const universeCreationDate = universe.creationDate
    ? ybFormatDate(universe.creationDate, YBTimeFormats.YB_DATE_ONLY_TIMESTAMP)
    : '';

  return (
    <Col sm={4} md={3} lg={2}>
      <Link to={'/universes/' + universe.universeUUID}>
        <div className="universe-display-item-container">
          <div className="status-icon">
            <UniverseStatusContainer
              currentUniverse={universe}
              refreshUniverseData={refreshUniverseData}
              shouldDisplayTaskButton={false}
            />
          </div>
          <div className="display-name">{universe.name}</div>
          <div className="provider-name">{universeProviderText}</div>
          <div className="description-item-list">
            <DescriptionItem title="Nodes">
              <span>{numNodes}</span>
            </DescriptionItem>
            <DescriptionItem title="Replication Factor">
              <span>{replicationFactor}</span>
            </DescriptionItem>
            <DescriptionItem title="Monthly Cost">
              <span>{costPerMonth}</span>
            </DescriptionItem>
            <DescriptionItem title="Created">
              <span>{universeCreationDate}</span>
            </DescriptionItem>
            <DescriptionItem title="Version">
              <span>
                {optimizeVersion(
                  primaryCluster?.userIntent.ybSoftwareVersion.split('-')[0].split('.')
                )}
              </span>
            </DescriptionItem>
          </div>
        </div>
      </Link>
    </Col>
  );
};
