// Copyright (c) YugabyteDB, Inc.

import Cookies from 'js-cookie';
import { setPerfAdvisorCustomHeaders } from '@yugabytedb/perf-advisor-ui';

// `perf-advisor-ui` ships its own bundled axios instance, so YBA's
// `axios.defaults.headers.common['Csrf-Token']` mutation in `actions/customers.js`
// does not reach PA UI's requests. Without a Csrf-Token header Play's default
// CSRFFilter rejects every state-changing PA UI call proxied through
// `/api/v1/customers/.../pa_proxy/...` with
// `[CSRF] Check failed because application/json ...`.
//
// From 1.0.153 onwards PA UI exposes `setPerfAdvisorCustomHeaders(getHeaders)`
// which registers a request interceptor on the internal axios instance and
// invokes `getHeaders` on every outgoing request, merging its return value into
// `request.headers.common`. Using a callback (rather than a snapshot) means we
// always read the current `csrfCookie` value, so the header stays in sync if
// Play issues a rotated token during the session.
//
// This module is imported for side effects from `RegisterYBAToPerfAdvisor.tsx`
// (the sole PA UI entry point in YBA); ES module caching ensures the
// registration runs exactly once per page load.
const CSRF_COOKIE_NAME = 'csrfCookie';
const CSRF_HEADER_NAME = 'Csrf-Token';

setPerfAdvisorCustomHeaders(() => {
  const csrfToken = Cookies.get(CSRF_COOKIE_NAME);
  return csrfToken ? { [CSRF_HEADER_NAME]: csrfToken } : {};
});
