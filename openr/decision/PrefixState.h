/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <set>
#include <unordered_map>
#include <vector>

#include <openr/common/NetworkUtil.h>
#include <openr/common/Types.h>
#include <openr/if/gen-cpp2/Decision_types.h>
#include <openr/if/gen-cpp2/Lsdb_types.h>
#include <openr/if/gen-cpp2/Network_types.h>

namespace openr {

class PrefixState {
 public:
  std::unordered_map<thrift::IpPrefix, PrefixEntries> const&
  prefixes() const {
    return prefixes_;
  }

  // update loopback prefix deletes
  void deleteLoopbackPrefix(
      thrift::IpPrefix const& prefix, const std::string& nodename);

  // returns set of changed prefixes (i.e. a node started advertising or
  // withdrew or any attributes changed)
  std::unordered_set<thrift::IpPrefix> updatePrefixDatabase(
      thrift::PrefixDatabase const& prefixDb);

  std::unordered_map<std::string /* nodeName */, thrift::PrefixDatabase>
  getPrefixDatabases() const;

  std::vector<thrift::NextHopThrift> getLoopbackVias(
      std::unordered_set<std::string> const& nodes, bool const isV4) const;

  std::unordered_map<std::string, thrift::BinaryAddress> const&
  getNodeHostLoopbacksV4() const {
    return nodeHostLoopbacksV4_;
  }

  std::unordered_map<std::string, thrift::BinaryAddress> const&
  getNodeHostLoopbacksV6() const {
    return nodeHostLoopbacksV6_;
  }

 private:
  // TODO: Maintain shared_ptr for `thrift::PrefixEntry` within
  // `PrefixEntries` to avoid data-copy for best metric selection and
  // route re-distribution
  // For each prefix in the network, stores a set of nodes that advertise it
  std::unordered_map<thrift::IpPrefix, PrefixEntries> prefixes_;
  std::unordered_map<
      std::string /* node */,
      std::unordered_map<std::string /* area */, std::set<thrift::IpPrefix>>>
      nodeToPrefixes_;
  std::unordered_map<std::string, thrift::BinaryAddress> nodeHostLoopbacksV4_;
  std::unordered_map<std::string, thrift::BinaryAddress> nodeHostLoopbacksV6_;
}; // class PrefixState

} // namespace openr
