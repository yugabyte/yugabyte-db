import { useCallback } from 'react';
import { useGetUserQuery, UserData } from '@app/api/src';
import type { Analytics } from '@segment/analytics-next'; // npm package needed for types only

// extend global Window type with segment analytics
declare global {
  interface Window {
    analytics?: Analytics;
  }
}

interface Options {
  skipGetUserQuery: boolean;
}

interface SegmentApi {
  segmentTrack: (eventName: string, props?: Record<string, unknown>) => void;
  segmentIdentify: (user: UserData, props?: Record<string, unknown>) => void;
  segmentPage: () => void;
}

type UseSegment = (options?: Options) => SegmentApi;

export const useSegment: UseSegment = (options) => {
  const { data: currentUser } = useGetUserQuery({
    query: {
      // take care of a corner case when useSegment() is on a page where a user is not authenticated
      enabled: !options?.skipGetUserQuery
    }
  });

  const segmentTrack = useCallback<SegmentApi['segmentTrack']>(
    (eventName, props) => {
      void window.analytics?.track(eventName, {
        ...props,
        user: {
          email: currentUser?.data?.spec?.email
        }
      });
    },
    [currentUser]
  );

  const segmentIdentify = useCallback<SegmentApi['segmentIdentify']>((user, props?) => {
    void window.analytics?.identify(user.info?.id, {
      ...props,
      firstName: user.spec?.first_name,
      lastName: user.spec?.last_name,
      email: user.spec?.email,
      company: user.spec?.company_name,
      createdOn: user.info?.metadata?.created_on, // date is ISO string
      screenResolution: `${window.screen.width}x${window.screen.height}`
    });
  }, []);

  const segmentPage = useCallback<SegmentApi['segmentPage']>(() => {
    void window.analytics?.page(undefined, undefined, {
      user: {
        email: currentUser?.data?.spec?.email ?? 'anonymous'
      }
    });
  }, [currentUser]);

  return { segmentTrack, segmentIdentify, segmentPage };
};
