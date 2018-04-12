// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#ifndef YB_CONSENSUS_CONSENSUS_FWD_H
#define YB_CONSENSUS_CONSENSUS_FWD_H

namespace yb {
namespace consensus {

class PeerProxyFactory;
class PeerMessageQueue;
class VoteRequestPB;
class VoteResponsePB;

class ConsensusServiceProxy;
typedef std::unique_ptr<ConsensusServiceProxy> ConsensusServiceProxyPtr;

class PeerProxy;
typedef std::unique_ptr<PeerProxy> PeerProxyPtr;

} // namespace consensus
} // namespace yb

#endif // YB_CONSENSUS_CONSENSUS_FWD_H
