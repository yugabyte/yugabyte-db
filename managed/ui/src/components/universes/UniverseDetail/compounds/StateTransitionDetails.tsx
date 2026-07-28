// Copyright (c) YugabyteDB, Inc.

import { FC, useEffect } from 'react';
import { createPortal } from 'react-dom';
import { useQuery } from 'react-query';
import { withRouter, WithRouterProps } from 'react-router';
import { YBLoading } from '@app/components/common/indicators';
import { api, universeQueryKey } from '@app/redesign/helpers/api';
import { colors } from '@app/redesign/theme/variables';
import { formatJsonHtml } from './stateTransitionJsonUtils';

type StateTransitionPageProps = WithRouterProps<{ uuid: string }>;

/**
 * Use the browser document scroll (not a fixed 100vh box). Chrome clips tall
 * <pre> content when global `pre { overflow-x: hidden }` combines with a
 * height-constrained scroll parent; #root.full-height also locks to 100%.
 */
function useDocumentScrollPage(): void {
  useEffect(() => {
    const root = document.getElementById('root');
    const { documentElement, body } = document;
    const previous = {
      rootDisplay: root?.style.display ?? '',
      htmlOverflow: documentElement.style.overflow,
      bodyOverflow: body.style.overflow,
      htmlHeight: documentElement.style.height,
      bodyHeight: body.style.height
    };

    if (root) {
      root.style.display = 'none';
    }
    documentElement.style.overflow = 'auto';
    body.style.overflow = 'auto';
    documentElement.style.height = 'auto';
    body.style.height = 'auto';

    return () => {
      if (root) {
        root.style.display = previous.rootDisplay;
      }
      documentElement.style.overflow = previous.htmlOverflow;
      body.style.overflow = previous.bodyOverflow;
      documentElement.style.height = previous.htmlHeight;
      body.style.height = previous.bodyHeight;
    };
  }, []);
}

const StateTransitionPage: FC<StateTransitionPageProps> = ({ params, location }) => {
  const universeUUID = params.uuid;
  const state =
    typeof location.query?.state === 'string' ? location.query.state : undefined;
  const { data, isLoading, isError, error } = useQuery(
    universeQueryKey.stateTransition(universeUUID, state),
    () => api.fetchStateTransition(universeUUID, state)
  );
  useDocumentScrollPage();

  const preStyle = {
    display: 'block',
    margin: 0,
    padding: 16,
    paddingBottom: 80,
    minHeight: '100vh',
    boxSizing: 'border-box' as const,
    // Override global `pre { overflow-x: hidden }` which forces overflow-y:auto in Chrome.
    overflow: 'visible' as const,
    overflowX: 'visible' as const,
    overflowY: 'visible' as const,
    whiteSpace: 'pre-wrap' as const,
    overflowWrap: 'anywhere' as const,
    background: colors.common.white,
    color: colors.common.black,
    fontFamily: 'monospace',
    fontSize: 13
  };

  if (isLoading) {
    return createPortal(
      <div
        style={{
          minHeight: '100vh',
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          background: colors.common.white
        }}
      >
        <YBLoading />
      </div>,
      document.body
    );
  }

  if (isError) {
    return createPortal(
      <pre style={preStyle}>
        {(error as Error)?.message ?? 'Failed to load state transition details.'}
      </pre>,
      document.body
    );
  }

  return createPortal(
    <pre
      style={preStyle}
      dangerouslySetInnerHTML={{ __html: formatJsonHtml(data ?? null) }}
    />,
    document.body
  );
};

export default withRouter(StateTransitionPage);
