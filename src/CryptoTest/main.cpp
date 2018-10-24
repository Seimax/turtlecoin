// Copyright (c) 2018, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#include <iostream>
#include <chrono>

#include <cxxopts.hpp>
#include <config/Ascii.h>
#include <config/CryptoNoteConfig.h>

#include "CryptoNote.h"
#include "CryptoTypes.h"
#include "Common/StringTools.h"
#include "crypto/crypto.h"
#include "version.h"

#define PERFORMANCE_ITERATIONS 1000

using namespace Crypto;
using namespace CryptoNote;

int main(int argc, char** argv) {
  std::string o_input;
  bool o_help, o_version, o_benchmark;
  int o_iterations;

  std::stringstream programHeader;
  programHeader << std::endl
    << asciiArt << std::endl
    << " " << CryptoNote::CRYPTONOTE_NAME << " v" << PROJECT_VERSION_LONG << std::endl
    << " This software is distributed under the General Public License v3.0" << std::endl << std::endl
    << " " << PROJECT_COPYRIGHT << std::endl << std::endl
    << " Additional Copyright(s) may apply, please see the included LICENSE file for more information." << std::endl
    << " If you did not receive a copy of the LICENSE, please visit:" << std::endl
    << " " << LICENSE_URL << std::endl << std::endl;

  cxxopts::Options options(argv[0], programHeader.str());

  options.add_options("Core")
    ("help",                          "Display this help message",
      cxxopts::value<bool>(o_help)
        ->implicit_value("true"))

    ("version",                       "Output software version information",
      cxxopts::value<bool>(o_version)
        ->default_value("false")
        ->implicit_value("true"))
    ;

  options.add_options("Data")
    ("input",                         "The hex encoded data to use for the hashing operations",
      cxxopts::value<std::string>(o_input),
      "hexstring")
  ;

  options.add_options("Performance Testing")
    ("benchmark",                     "Run quick performance benchmark",
      cxxopts::value<bool>(o_benchmark)
        ->default_value("false")
        ->implicit_value("true"))

    ("iterations",                    "The number of iterations for the benchmark test",
      cxxopts::value<int>(o_iterations)
        ->default_value(std::to_string(PERFORMANCE_ITERATIONS)),
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

  try {
    const BinaryArray& rawData = Common::fromHex(o_input);

    std::cout << programHeader.str() << std::endl;

    std::cout << "Input: " << o_input << std::endl << std::endl;

    Hash hash = Hash();

    cn_fast_hash(rawData.data(), rawData.size(), hash);
    std::cout << "cn_fast_hash: " << Common::toHex(&hash, sizeof(Hash)) << std::endl;

    cn_slow_hash_v0(rawData.data(), rawData.size(), hash);
    std::cout << "cn_slow_hash_v0: " << Common::toHex(&hash, sizeof(Hash)) << std::endl;

    if (rawData.size() >= 43)
    {
      cn_slow_hash_v1(rawData.data(), rawData.size(), hash);
      std::cout << "cn_slow_hash_v1: " << Common::toHex(&hash, sizeof(Hash)) << std::endl;

      cn_slow_hash_v2(rawData.data(), rawData.size(), hash);
      std::cout << "cn_slow_hash_v2: " << Common::toHex(&hash, sizeof(Hash)) << std::endl;

      cn_lite_slow_hash_v0(rawData.data(), rawData.size(), hash);
      std::cout << "cn_lite_slow_hash_v0: " << Common::toHex(&hash, sizeof(Hash)) << std::endl;

      cn_lite_slow_hash_v1(rawData.data(), rawData.size(), hash);
      std::cout << "cn_lite_slow_hash_v1: " << Common::toHex(&hash, sizeof(Hash)) << std::endl;

      cn_lite_slow_hash_v2(rawData.data(), rawData.size(), hash);
      std::cout << "cn_lite_slow_hash_v2: " << Common::toHex(&hash, sizeof(Hash)) << std::endl;
    }

    if (o_benchmark)
    {
      std::cout << std::endl << "Performance Tests: Please wait, this may take a while depending on your system..." << std::endl << std::endl;

      auto startTimer = std::chrono::high_resolution_clock::now();
      for (auto i = 0; i < o_iterations; i++)
      {
        cn_slow_hash_v0(rawData.data(), rawData.size(), hash);
      }
      auto elapsedTime = std::chrono::high_resolution_clock::now() - startTimer;
      std::cout << "cn_slow_hash_v0: " << (o_iterations / std::chrono::duration_cast<std::chrono::seconds>(elapsedTime).count()) << " H/s" << std::endl;

      startTimer = std::chrono::high_resolution_clock::now();
      for (auto i = 0; i < o_iterations; i++)
      {
        cn_lite_slow_hash_v0(rawData.data(), rawData.size(), hash);
      }
      elapsedTime = std::chrono::high_resolution_clock::now() - startTimer;
      std::cout << "cn_lite_slow_hash_v0: " << (o_iterations / std::chrono::duration_cast<std::chrono::seconds>(elapsedTime).count()) << " H/s" << std::endl;
    }
  }
  catch (std::exception& e)
  {
    std::cout << "Something went terribly wrong..." << std::endl << e.what() << std::endl << std::endl;
  }

  return 0;
}