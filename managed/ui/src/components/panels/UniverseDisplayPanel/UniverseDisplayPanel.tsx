// Copyright (c) YugaByte, Inc.

import { Link } from 'react-router';
import { Row, Col } from 'react-bootstrap';
import { UniverseCard } from './UniverseCard';
import { useQuery } from 'react-query';
import { makeStyles } from '@material-ui/core';

import { RbacValidator } from '../../../redesign/features/rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../redesign/features/rbac/ApiAndUserPermMapping';
import { isDisabled, isNotHidden } from '../../../utils/LayoutUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { YBLoading } from '../../common/indicators';
import { YBButton } from '../../common/forms/fields';
import {
  RunTimeConfigEntry,
  Universe
} from '../../../redesign/features/universe/universe-form/utils/dto';
import { api, runtimeConfigQueryKey } from '../../../redesign/helpers/api';
import { RuntimeConfigKey } from '../../../redesign/helpers/constants';
import { getIsNodeAgentEnabled } from '../../../redesign/features/NodeAgent/utils';
import { NodeAgentAPI, QUERY_KEY } from '../../../redesign/features/NodeAgent/api';
import { DEFAULT_RUNTIME_GLOBAL_SCOPE } from '../../../actions/customers';
import { YBProvider } from '../../configRedesign/providerRedesign/types';
import { getUniverseStatus, UniverseState } from '../../universes/helpers/universeHelpers';
import { InstallNodeAgentReminderBanner } from '../../../redesign/features/NodeAgent/InstallNodeAgentReminderBanner';

import './UniverseDisplayPanel.scss';

export const UniverseDisplayPanel = ({
  universe: { universeList },
  cloud: { providers },
  customer: { currentCustomer },
  runtimeConfigs,
  fetchUniverseMetadata
}: any) => {
  const providerUuidToName = {};
  (providers.data as YBProvider[]).forEach(
    (provider) => (providerUuidToName[provider.uuid] = provider.name)
  );

  const pausedUniverseUuids = new Set<string>();
  const inUseProviderUuids = new Set<string>();
  universeList.data?.forEach((universe: Universe) => {
    const universeStatus = getUniverseStatus(universe);
    if (universeStatus.state === UniverseState.PAUSED) {
      pausedUniverseUuids.add(universe.universeUUID);
    }
    universe.universeDetails.clusters.forEach((cluster) => {
      const providerUuid = cluster.userIntent.provider;
      inUseProviderUuids.add(providerUuid);
    });
  });

  const globalRuntimeConfigQuery = useQuery(runtimeConfigQueryKey.globalScope(), () =>
    api.fetchRuntimeConfigs(DEFAULT_RUNTIME_GLOBAL_SCOPE)
  );
  const nodeAgentsQuery = useQuery(QUERY_KEY.fetchNodeAgents, () => NodeAgentAPI.fetchNodeAgents());
  const hasNodeAgentFailures = (nodeAgentsQuery.data ?? []).some(
    (nodeAgent) =>
      nodeAgent.lastError?.code &&
      // Ignore errors for nodes agents on paused universes as
      // they are expected to be unreachable.
      !pausedUniverseUuids.has(nodeAgent.universeUuid) &&
      // Ensure the node agent has a universe uuid attached to it so we can issue
      // the node agent install request.
      nodeAgent.universeUuid
  );
  if (getPromiseState(providers).isSuccess()) {
    let universeDisplayList = <span />;
    if (getPromiseState(universeList).isSuccess()) {
      universeDisplayList = universeList.data
        .sort((a: any, b: any) => {
          return Date.parse(a.creationDate) < Date.parse(b.creationDate);
        })
        .map((universeItem: any) => {
          return (
            <UniverseCard
              key={universeItem.name}
              universe={universeItem}
              providers={providers}
              refreshUniverseData={fetchUniverseMetadata}
              runtimeConfigs={runtimeConfigs}
            />
          );
        });
    }
    const nodeAgentEnablerScanInterval =
      globalRuntimeConfigQuery.data?.configEntries?.find(
        (configEntry: RunTimeConfigEntry) =>
          configEntry.key === RuntimeConfigKey.NODE_AGENT_ENABLER_SCAN_INTERVAL
      )?.value ?? '';
    const isNodeAgentEnabled = getIsNodeAgentEnabled(nodeAgentEnablerScanInterval);

    const showNodeAgentBanner = isNodeAgentEnabled && hasNodeAgentFailures;
    return (
      <div className="universe-display-panel-container">
        <Row>
          <Col xs={3}>
            <h2>Universes</h2>
          </Col>
          <Col className="universe-table-header-action dashboard-universe-actions">
            {isNotHidden(currentCustomer.data.features, 'universe.create') && (
              <RbacValidator
                accessRequiredOn={{
                  ...ApiPermissionMap.CREATE_UNIVERSE
                }}
                isControl
              >
                <Link to="/universes/create">
                  <YBButton
                    btnClass="universe-button btn btn-lg btn-orange"
                    disabled={isDisabled(currentCustomer.data.features, 'universe.create')}
                    btnText="Create Universe"
                    btnIcon="fa fa-plus"
                    data-testid="Dashboard-CreateUniverse"
                  />
                </Link>
              </RbacValidator>
            )}
          </Col>
        </Row>
        {showNodeAgentBanner && <InstallNodeAgentReminderBanner />}
        <Row className="list-group">{universeDisplayList}</Row>
      </div>
    );
  } else if (getPromiseState(providers).isEmpty()) {
    return (
      <div className="get-started-config">
        <span className="yb-data-name">
          Welcome to the <div>YugaByte Admin Console.</div>
        </span>
        <span>Before you can create a Universe, you must configure a cloud provider.</span>
        <Link to={'config'}>
          <div className="create-universe-button">
            <div className="btn-icon">
              <i className="fa fa-plus" />
            </div>
            <div className="display-name text-center">{'Configure a Provider'}</div>
          </div>
        </Link>
      </div>
    );
  } else {
    return <YBLoading />;
  }
};
