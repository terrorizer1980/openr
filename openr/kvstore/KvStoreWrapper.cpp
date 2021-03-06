/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "KvStoreWrapper.h"

#include <memory>

#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <openr/common/Constants.h>

namespace {

const int kSocketHwm = 99999;

} // anonymous namespace

namespace openr {

KvStoreWrapper::KvStoreWrapper(
    fbzmq::Context& zmqContext,
    std::shared_ptr<const Config> config,
    std::optional<messaging::RQueue<thrift::PeerUpdateRequest>>
        peerUpdatesQueue,
    bool enableKvStoreThrift)
    : nodeId(config->getNodeName()),
      globalCmdUrl(folly::sformat("inproc://{}-kvstore-global-cmd", nodeId)),
      enableFloodOptimization_(
          config->getKvStoreConfig().enable_flood_optimization_ref().value_or(
              false)),
      enableKvStoreThrift_(enableKvStoreThrift) {
  VLOG(1) << "KvStoreWrapper: Creating KvStore.";
  kvStore_ = std::make_unique<KvStore>(
      zmqContext,
      kvStoreUpdatesQueue_,
      peerUpdatesQueue.has_value() ? peerUpdatesQueue.value()
                                   : dummyPeerUpdatesQueue_.getReader(),
      KvStoreGlobalCmdUrl{globalCmdUrl},
      MonitorSubmitUrl{folly::sformat("inproc://{}-monitor-submit", nodeId)},
      config,
      std::nullopt /* ip-tos */,
      Constants::kHighWaterMark,
      enableKvStoreThrift_);
}

void
KvStoreWrapper::run() noexcept {
  // Start kvstore
  kvStoreThread_ = std::thread([this]() {
    VLOG(1) << "KvStore " << nodeId << " running.";
    kvStore_->run();
    VLOG(1) << "KvStore " << nodeId << " stopped.";
  });
  kvStore_->waitUntilRunning();
}

void
KvStoreWrapper::stop() {
  // Return immediately if not running
  if (!kvStore_->isRunning()) {
    return;
  }

  // Close queue
  kvStoreUpdatesQueue_.close();
  dummyPeerUpdatesQueue_.close();

  // Stop kvstore
  kvStore_->stop();
  kvStoreThread_.join();
}

bool
KvStoreWrapper::setKey(
    std::string key,
    thrift::Value value,
    std::optional<std::vector<std::string>> nodeIds,
    std::string area) {
  // Prepare KeySetParams
  thrift::KeySetParams params;
  params.keyVals_ref()->emplace(std::move(key), std::move(value));
  params.nodeIds_ref().from_optional(std::move(nodeIds));

  try {
    kvStore_->setKvStoreKeyVals(std::move(params), area).get();
  } catch (std::exception const& e) {
    LOG(ERROR) << "Exception to set key in kvstore: " << folly::exceptionStr(e);
    return false;
  }
  return true;
}

bool
KvStoreWrapper::setKeys(
    const std::vector<std::pair<std::string, thrift::Value>>& keyVals,
    std::optional<std::vector<std::string>> nodeIds,
    std::string area) {
  // Prepare KeySetParams
  thrift::KeySetParams params;
  params.nodeIds_ref().from_optional(std::move(nodeIds));
  for (const auto& keyVal : keyVals) {
    params.keyVals_ref()->emplace(keyVal.first, keyVal.second);
  }

  try {
    kvStore_->setKvStoreKeyVals(std::move(params), area).get();
  } catch (std::exception const& e) {
    LOG(ERROR) << "Exception to set key in kvstore: " << folly::exceptionStr(e);
    return false;
  }
  return true;
}

std::optional<thrift::Value>
KvStoreWrapper::getKey(std::string key, std::string area) {
  // Prepare KeyGetParams
  thrift::KeyGetParams params;
  params.keys_ref()->push_back(key);

  thrift::Publication pub;
  try {
    pub = *(kvStore_->getKvStoreKeyVals(std::move(params), area).get());
  } catch (std::exception const& e) {
    LOG(WARNING) << "Exception to get key from kvstore: "
                 << folly::exceptionStr(e);
    return std::nullopt; // No value found
  }

  // Return the result
  auto it = pub.keyVals_ref()->find(key);
  if (it == pub.keyVals_ref()->end()) {
    return std::nullopt; // No value found
  }
  return it->second;
}

std::unordered_map<std::string /* key */, thrift::Value>
KvStoreWrapper::dumpAll(
    std::optional<KvStoreFilters> filters, std::string area) {
  // Prepare KeyDumpParams
  thrift::KeyDumpParams params;
  if (filters.has_value()) {
    std::string keyPrefix = folly::join(",", filters.value().getKeyPrefixes());
    params.prefix_ref() = keyPrefix;
    params.originatorIds_ref() = filters.value().getOriginatorIdList();
    if (not keyPrefix.empty()) {
      params.keys_ref() = filters.value().getKeyPrefixes();
    }
  }

  auto pub = *(kvStore_->dumpKvStoreKeys(std::move(params), area).get());
  return *pub.keyVals_ref();
}

std::unordered_map<std::string /* key */, thrift::Value>
KvStoreWrapper::dumpHashes(std::string const& prefix, std::string area) {
  // Prepare KeyDumpParams
  thrift::KeyDumpParams params;
  params.prefix_ref() = prefix;
  params.keys_ref() = {prefix};

  auto pub = *(kvStore_->dumpKvStoreHashes(std::move(params), area).get());
  return *pub.keyVals_ref();
}

std::unordered_map<std::string /* key */, thrift::Value>
KvStoreWrapper::syncKeyVals(
    thrift::KeyVals const& keyValHashes, std::string area) {
  // Prepare KeyDumpParams
  thrift::KeyDumpParams params;
  params.keyValHashes_ref() = keyValHashes;

  auto pub = *(kvStore_->dumpKvStoreKeys(std::move(params), area).get());
  return *pub.keyVals_ref();
}

thrift::Publication
KvStoreWrapper::recvPublication() {
  auto maybePublication = kvStoreUpdatesQueueReader_.get(); // perform read
  if (maybePublication.hasError()) {
    throw std::runtime_error(std::string("recvPublication failed"));
  }
  auto pub = maybePublication.value();
  return pub;
}

thrift::SptInfos
KvStoreWrapper::getFloodTopo(std::string area) {
  auto sptInfos = *(kvStore_->getSpanningTreeInfos(area).get());
  return sptInfos;
}

bool
KvStoreWrapper::addPeer(
    std::string peerName, thrift::PeerSpec spec, std::string area) {
  // Prepare peerAddParams
  thrift::PeerAddParams params;
  params.peers_ref()->emplace(peerName, spec);

  try {
    kvStore_->addUpdateKvStorePeers(params, area).get();
  } catch (std::exception const& e) {
    LOG(ERROR) << "Failed to add peers: " << folly::exceptionStr(e);
    return false;
  }
  return true;
}

bool
KvStoreWrapper::delPeer(std::string peerName, std::string area) {
  // Prepare peerDelParams
  thrift::PeerDelParams params;
  params.peerNames_ref()->emplace_back(peerName);

  try {
    kvStore_->deleteKvStorePeers(params, area).get();
  } catch (std::exception const& e) {
    LOG(ERROR) << "Failed to delete peers: " << folly::exceptionStr(e);
    return false;
  }
  return true;
}

std::optional<KvStorePeerState>
KvStoreWrapper::getPeerState(
    std::string const& peerName, std::string const& area) {
  return kvStore_->getKvStorePeerState(peerName, area).get();
}

std::unordered_map<std::string /* peerName */, thrift::PeerSpec>
KvStoreWrapper::getPeers(std::string area) {
  auto peers = *(kvStore_->getKvStorePeers(area).get());
  return peers;
}

} // namespace openr
