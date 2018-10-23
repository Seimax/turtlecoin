// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2018, The TurtleCoin Developers
// Copyright (c) 2018, The Karai Developers
//
// Please see the included LICENSE file for more information.

#include <fstream>

#include "Daemon.h"
#include <cxxopts.hpp>
#include <config/Ascii.h>

#include "DaemonCommandsHandler.h"
#include "Common/ScopeExit.h"
#include "Common/SignalHandler.h"
#include "Common/StdOutputStream.h"
#include "Common/StdInputStream.h"
#include "Common/PathTools.h"
#include "Common/Util.h"
#include "crypto/hash.h"
#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/Core.h"
#include "CryptoNoteCore/Currency.h"
#include "CryptoNoteCore/DatabaseBlockchainCache.h"
#include "CryptoNoteCore/DatabaseBlockchainCacheFactory.h"
#include "CryptoNoteCore/MainChainStorage.h"
#include "CryptoNoteCore/RocksDBWrapper.h"
#include "CryptoNoteProtocol/CryptoNoteProtocolHandler.h"
#include "P2p/NetNode.h"
#include "P2p/NetNodeConfig.h"
#include "Rpc/RpcServer.h"
#include "Serialization/BinaryInputStreamSerializer.h"
#include "Serialization/BinaryOutputStreamSerializer.h"
#include "version.h"

#include <config/CryptoNoteCheckpoints.h>
#include <Logging/LoggerManager.h>

#if defined(WIN32)
#include <crtdbg.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using Common::JsonValue;
using namespace CryptoNote;
using namespace Logging;

void print_genesis_tx_hex(const std::vector<std::string> rewardAddresses, const bool blockExplorerMode, LoggerManager& logManager)
{
  std::vector<CryptoNote::AccountPublicAddress> rewardTargets;

  CryptoNote::CurrencyBuilder currencyBuilder(logManager);
  currencyBuilder.isBlockexplorer(blockExplorerMode);

  CryptoNote::Currency currency = currencyBuilder.currency();

  for (const auto& rewardAddress : rewardAddresses)
  {
    CryptoNote::AccountPublicAddress address;
    if (!currency.parseAccountAddressString(rewardAddress, address))
    {
      std::cout << "Failed to parse genesis reward address: " << rewardAddress << std::endl;
      return;
    }
    rewardTargets.emplace_back(std::move(address));
  }

  CryptoNote::Transaction transaction;

  if (rewardTargets.empty())
  {
    if (CryptoNote::parameters::GENESIS_BLOCK_REWARD > 0)
    {
      std::cout << "Error: Genesis Block Reward Addresses are not defined" << std::endl;
      return;
    }
    transaction = CryptoNote::CurrencyBuilder(logManager).generateGenesisTransaction();
  }
  else
  {
    transaction = CryptoNote::CurrencyBuilder(logManager).generateGenesisTransaction();
  }

  std::string transactionHex = Common::toHex(CryptoNote::toBinaryArray(transaction));
  std::cout << "Replace the current GENESIS_COINBASE_TX_HEX line in src/config/CryptoNoteConfig.h with this one:" << std::endl;
  std::cout << "const char GENESIS_COINBASE_TX_HEX[] = \"" << transactionHex << "\";" << std::endl;

  return;
}

JsonValue buildLoggerConfiguration(Level level, const std::string& logfile) {
  JsonValue loggerConfiguration(JsonValue::OBJECT);
  loggerConfiguration.insert("globalLevel", static_cast<int64_t>(level));

  JsonValue& cfgLoggers = loggerConfiguration.insert("loggers", JsonValue::ARRAY);

  JsonValue& fileLogger = cfgLoggers.pushBack(JsonValue::OBJECT);
  fileLogger.insert("type", "file");
  fileLogger.insert("filename", logfile);
  fileLogger.insert("level", static_cast<int64_t>(TRACE));

  JsonValue& consoleLogger = cfgLoggers.pushBack(JsonValue::OBJECT);
  consoleLogger.insert("type", "console");
  consoleLogger.insert("level", static_cast<int64_t>(TRACE));
  consoleLogger.insert("pattern", "%D %T %L ");

  return loggerConfiguration;
}

/* Wait for input so users can read errors before the window closes if they
   launch from a GUI rather than a terminal */
void pause_for_input(int argc) {
  /* if they passed arguments they're probably in a terminal so the errors will
     stay visible */
  if (argc == 1) {
    #if defined(WIN32)
    if (_isatty(_fileno(stdout)) && _isatty(_fileno(stdin))) {
    #else
    if(isatty(fileno(stdout)) && isatty(fileno(stdin))) {
    #endif
      std::cout << "Press any key to close the program: ";
      getchar();
    }
  }
}

int main(int argc, char* argv[])
{

#ifdef WIN32
  _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

  LoggerManager logManager;
  LoggerRef logger(logManager, "daemon");

  std::stringstream programHeader;
  programHeader << std::endl
    << asciiArt << std::endl
    << " " << CryptoNote::CRYPTONOTE_NAME << " v" << PROJECT_VERSION_LONG << std::endl
    << " This software is distributed under the General Public License v3.0" << std::endl << std::endl
    << " " << PROJECT_COPYRIGHT << std::endl << std::endl
    << " Additional Copyright(s) may apply, please see the included LICENSE file for more information." << std::endl
    << " If you did not receive a copy of the LICENSE, please visit:" << std::endl
    << " " << LICENSE_URL << std::endl << std::endl;

  std::stringstream h_configFile;
  h_configFile << CryptoNote::CRYPTONOTE_NAME << ".conf";

  cxxopts::Options options(argv[0], programHeader.str());

  options.add_options("Core")
    ("help",                          "Display this help message",
      cxxopts::value<bool>(o_help)
        ->implicit_value("true"))

    ("os-version",                    "Output Operating System version information",
      cxxopts::value<bool>(o_osVersion)
        ->default_value("false")
        ->implicit_value("true"))

    ("version",                       "Output daemon version information",
      cxxopts::value<bool>(o_version)
        ->default_value("false")
        ->implicit_value("true"))
    ;

  options.add_options("Genesis Block")
    ("genesis-block-reward-address",  "Specify the address for any premine genesis block rewards",
      cxxopts::value<std::vector<std::string>>(o_genesisAwardAddresses),
      "<address>")

    ("print-genesis-tx",              "Print the genesis block transaction hex and exits",
      cxxopts::value<bool>(o_printGenesisTx)
        ->default_value("false")
        ->implicit_value("true"))
    ;

  options.add_options("Daemon")
    ("config-file",                   "Specify the location of a configuration file",
      cxxopts::value<std::string>(o_configFile)
        ->default_value("")
        ->implicit_value(h_configFile.str()),
        "PATH")

    ("data-dir",                      "Specify Blockchain Data Directory",
      cxxopts::value<std::string>(o_dataDirectory)
        ->default_value(Tools::getDefaultDataDirectory()),
        "PATH")

    ("load-checkpoints",              "Use builtin default checkpoints or checkpoint csv file for faster initial Blockchain sync",
      cxxopts::value<std::string>(o_checkPoints)
        ->default_value("default")
        ->implicit_value("default"))

    ("log-file",                      "Specify log file location",
      cxxopts::value<std::string>(o_LogFile)
        ->default_value(Common::ReplaceExtenstion(Common::NativePathToGeneric(argv[0]), ".log")),
        "PATH")

    ("log-level",                     "Specify log level",
      cxxopts::value<int>(o_logLevel)
        ->default_value("2"),
        "#")

    ("no-console",                    "Disable daemon console commands",
      cxxopts::value<bool>(o_noConsole)
        ->default_value("false")
        ->implicit_value("true"))
    ;

  options.add_options("RPC")
    ("enable-blockexplorer",          "Enable the Blockchain Explorer RPC",
      cxxopts::value<bool>(o_enableBlockExplorer)
        ->default_value("false")
        ->implicit_value("true"))

    ("enable-cors",                   "Adds header 'Access-Control-Allow-Origin' to the RPC responses. Uses the value specified as the domain. Use * for all.",
      cxxopts::value<std::vector<std::string>>(o_enableCors)
        ->implicit_value("*"),
        "STRING")

    ("fee-address",                   "Sets the convenience charge address for light wallets that use the daemon",
      cxxopts::value<std::string>(o_feeAddress),
      "<address>")

    ("fee-amount",                    "Sets the convenience charge amount for light wallets that use the daemon",
      cxxopts::value<int>(o_feeAmount)
        ->default_value("0"),
        "#")
    ;

  options.add_options("Network")
    ("allow-local-ip",                "Allow the local IP to be added to the peer list",
      cxxopts::value<bool>(o_localIp)
        ->default_value("false")
        ->implicit_value("true"))

    ("hide-my-port",                  "Do not announce yourself as a peerlist candidate",
      cxxopts::value<bool>(o_hideMyPort)
        ->default_value("false")
        ->implicit_value("true"))

    ("p2p-bind-ip",                   "Interface IP address for the P2P service",
      cxxopts::value<std::string>(o_p2pInterface)
        ->default_value("0.0.0.0"),
        "<ip>")

    ("p2p-bind-port",                 "TCP port for the P2P service",
      cxxopts::value<int>(o_p2pPort)
        ->default_value(std::to_string(CryptoNote::P2P_DEFAULT_PORT)),
        "#")

    ("p2p-external-port",             "External TCP port for the P2P service (NAT port forward)",
      cxxopts::value<int>(o_p2pExternalPort)
        ->default_value("0"),
        "#")

    ("rpc-bind-ip",                   "Interface IP address for the RPC service",
      cxxopts::value<std::string>(o_rpcInterface)
        ->default_value("127.0.0.1"),
        "<ip>")

    ("rpc-bind-port",                 "TCP port for the RPC service",
      cxxopts::value<int>(o_rpcPort)
        ->default_value(std::to_string(CryptoNote::RPC_DEFAULT_PORT)),
        "#")
    ;

  options.add_options("Peer")
    ("add-exclusive-node",            "Manually add a peer to the local peer list ONLY attempt connections to it. [ip:port]",
      cxxopts::value<std::vector<std::string>>(o_exclusiveNodes),
      "<ip:port>")

    ("add-peer",                      "Manually add a peer to the local peer list",
      cxxopts::value<std::vector<std::string>>(o_peers),
      "<ip:port>")

    ("add-priority-node",             "Manually add a peer to the local peer list and attempt to maintain a connection to it [ip:port]",
      cxxopts::value<std::vector<std::string>>(o_priorityNodes),
      "<ip:port>")

    ("seed-node",                     "Connect to a node to retrieve the peer list and then disconnect",
      cxxopts::value<std::vector<std::string>>(o_seedNodes),
      "<ip:port>")
    ;

  options.add_options("Database")
    ("db-max-open-files",             "Number of files that can be used by the database at one time",
      cxxopts::value<int>(o_dbMaxOpenFiles)
        ->default_value(std::to_string(CryptoNote::DATABASE_DEFAULT_MAX_OPEN_FILES)),
        "#")

    ("db-read-buffer-size",           "Size of the database read cache in megabytes (MB)",
      cxxopts::value<int>(o_dbReadCacheSize)
        ->default_value(std::to_string(CryptoNote::DATABASE_READ_BUFFER_MB_DEFAULT_SIZE)),
        "#")

    ("db-threads",                    "Number of background threads used for compaction and flush operations",
      cxxopts::value<int>(o_dbThreads)
        ->default_value(std::to_string(CryptoNote::DATABASE_DEFAULT_BACKGROUND_THREADS_COUNT)),
        "#")

    ("db-write-buffer-size",          "Size of the database write buffer in megabytes (MB)",
      cxxopts::value<int>(o_dbWriteBufferSize)
        ->default_value(std::to_string(CryptoNote::DATABASE_WRITE_BUFFER_MB_DEFAULT_SIZE)),
        "#")
    ;

  try
  {
    auto result = options.parse(argc, argv);
  }
  catch (const cxxopts::OptionException& e)
  {
    std::cout << "Error: Unable to parse command line argument options: " << e.what() << std::endl << std::endl;
    std::cout << options.help({}) << std::endl;
    exit(1);
  }

  try {

    if (o_help) // Do we want to display the help message?
    {
      std::cout << options.help({}) << std::endl;
      exit(0);
    }
    else if (o_version) // Do we want to display the software version?
    {
      std::cout << programHeader.str() << std::endl;
      exit(0);
    }
    else if (o_osVersion) // Do we want to display the OS version information?
    {
      std::cout << programHeader.str()
      << "OS: " << Tools::get_os_version_string() << std::endl;
      exit(0);
    }
    else if (o_printGenesisTx) // Do we weant to generate the Genesis Tx?
    {
      print_genesis_tx_hex(o_genesisAwardAddresses, o_enableBlockExplorer, logManager);
      exit(0);
    }

    auto modulePath = Common::NativePathToGeneric(argv[0]);
    auto cfgLogFile = Common::NativePathToGeneric(o_configFile);

    if (cfgLogFile.empty()) {
      cfgLogFile = Common::ReplaceExtenstion(modulePath, ".log");
    } else {
      if (!Common::HasParentPath(cfgLogFile)) {
        cfgLogFile = Common::CombinePath(Common::GetPathDirectory(modulePath), cfgLogFile);
      }
    }

    Level cfgLogLevel = static_cast<Level>(static_cast<int>(Logging::ERROR) + o_logLevel);

    // configure logging
    logManager.configure(buildLoggerConfiguration(cfgLogLevel, cfgLogFile));

    logger(INFO, BRIGHT_GREEN) << programHeader.str() << std::endl;

    logger(INFO) << "Program Working Directory: " << argv[0];

    //create objects and link them
    CryptoNote::CurrencyBuilder currencyBuilder(logManager);
    currencyBuilder.isBlockexplorer(o_enableBlockExplorer);

    try {
      currencyBuilder.currency();
    } catch (std::exception&) {
      std::cout << "GENESIS_COINBASE_TX_HEX constant has an incorrect value. Please launch: " << CryptoNote::CRYPTONOTE_NAME << "d --print-genesis-tx" << std::endl;
      return 1;
    }
    CryptoNote::Currency currency = currencyBuilder.currency();

    bool use_checkpoints = !o_checkPoints.empty();
    CryptoNote::Checkpoints checkpoints(logManager);

    if (use_checkpoints) {
      logger(INFO) << "Loading Checkpoints for faster initial sync...";
      if (o_checkPoints == "default") {
        for (const auto& cp : CryptoNote::CHECKPOINTS) {
          checkpoints.addCheckpoint(cp.index, cp.blockId);
        }
        logger(INFO) << "Loaded " << CryptoNote::CHECKPOINTS.size() << " default checkpoints";
      } else {
        bool results = checkpoints.loadCheckpointsFromFile(o_checkPoints);
        if (!results) {
          throw std::runtime_error("Failed to load checkpoints");
        }
      }
    }

    NetNodeConfig netNodeConfig;
    netNodeConfig.init(o_p2pInterface, o_p2pPort, o_p2pExternalPort, o_localIp,
                        o_hideMyPort, o_dataDirectory, o_peers,
                        o_exclusiveNodes, o_priorityNodes,
                        o_seedNodes);

    DataBaseConfig dbConfig;
    dbConfig.init(o_dataDirectory, o_dbThreads, o_dbMaxOpenFiles, o_dbWriteBufferSize, o_dbReadCacheSize);

    if (dbConfig.isConfigFolderDefaulted()) {
      if (!Tools::create_directories_if_necessary(dbConfig.getDataDir())) {
        throw std::runtime_error("Can't create directory: " + dbConfig.getDataDir());
      }
    } else {
      if (!Tools::directoryExists(dbConfig.getDataDir())) {
        throw std::runtime_error("Directory does not exist: " + dbConfig.getDataDir());
      }
    }

    RocksDBWrapper database(logManager);
    database.init(dbConfig);
    Tools::ScopeExit dbShutdownOnExit([&database] () { database.shutdown(); });

    if (!DatabaseBlockchainCache::checkDBSchemeVersion(database, logManager))
    {
      dbShutdownOnExit.cancel();
      database.shutdown();

      database.destroy(dbConfig);

      database.init(dbConfig);
      dbShutdownOnExit.resume();
    }

    System::Dispatcher dispatcher;
    logger(INFO) << "Initializing core...";
    CryptoNote::Core ccore(
      currency,
      logManager,
      std::move(checkpoints),
      dispatcher,
      std::unique_ptr<IBlockchainCacheFactory>(new DatabaseBlockchainCacheFactory(database, logger.getLogger())),
      createSwappedMainChainStorage(o_dataDirectory, currency));

    ccore.load();
    logger(INFO) << "Core initialized OK";

    CryptoNote::CryptoNoteProtocolHandler cprotocol(currency, dispatcher, ccore, nullptr, logManager);
    CryptoNote::NodeServer p2psrv(dispatcher, cprotocol, logManager);
    CryptoNote::RpcServer rpcServer(dispatcher, logManager, ccore, p2psrv, cprotocol);

    cprotocol.set_p2p_endpoint(&p2psrv);
    DaemonCommandsHandler dch(ccore, p2psrv, logManager, &rpcServer);
    logger(INFO) << "Initializing p2p server...";
    if (!p2psrv.init(netNodeConfig)) {
      logger(ERROR, BRIGHT_RED) << "Failed to initialize p2p server.";
      return 1;
    }

    logger(INFO) << "P2p server initialized OK";

    if (!o_noConsole) {
      dch.start_handling();
    }

    // Fire up the RPC Server
    logger(INFO) << "Starting core rpc server on address " << o_rpcInterface;
    rpcServer.start(o_rpcInterface, o_rpcPort);
    rpcServer.setFeeAddress(o_feeAddress);
    rpcServer.setFeeAmount(o_feeAmount);
    rpcServer.enableCors(o_enableCors);
    logger(INFO) << "Core rpc server started ok";

    Tools::SignalHandler::install([&dch, &p2psrv] {
      dch.stop_handling();
      p2psrv.sendStopSignal();
    });

    logger(INFO) << "Starting p2p net loop...";
    p2psrv.run();
    logger(INFO) << "p2p net loop stopped";

    dch.stop_handling();

    //stop components
    logger(INFO) << "Stopping core rpc server...";
    rpcServer.stop();

    //deinitialize components
    logger(INFO) << "Deinitializing p2p...";
    p2psrv.deinit();

    cprotocol.set_p2p_endpoint(nullptr);
    ccore.save();

  } catch (const std::exception& e) {
    logger(ERROR, BRIGHT_RED) << "Exception: " << e.what();
    return 1;
  }

  logger(INFO) << "Node stopped.";
  return 0;
}
