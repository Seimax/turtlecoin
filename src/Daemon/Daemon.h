// Copyright (c) 2018, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include <vector>

namespace {
  std::string o_configFile, o_dataDirectory, o_LogFile, o_feeAddress, o_rpcInterface, o_p2pInterface, o_checkPoints;
  std::vector<std::string> o_peers, o_priorityNodes, o_exclusiveNodes, o_seedNodes, o_genesisAwardAddresses, o_enableCors;
  int o_logLevel, o_feeAmount, o_rpcPort, o_p2pPort, o_p2pExternalPort, o_dbThreads, o_dbMaxOpenFiles, o_dbWriteBufferSize, o_dbReadCacheSize;
  bool o_help, o_version, o_osVersion, o_noConsole, o_enableBlockExplorer, o_printGenesisTx, o_localIp, o_hideMyPort;
}