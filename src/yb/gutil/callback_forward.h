// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef YB_GUTIL_CALLBACK_FORWARD_H_
#define YB_GUTIL_CALLBACK_FORWARD_H_

namespace yb {

template <typename Sig>
class Callback;

typedef Callback<void(void)> Closure;

}  // namespace yb

#endif // YB_GUTIL_CALLBACK_FORWARD_H
