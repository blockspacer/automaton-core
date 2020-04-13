#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include <curl/curl.h>  // NOLINT
#include <json.hpp>

#include "automaton/core/interop/ethereum/eth_contract_curl.h"
#include "automaton/core/interop/ethereum/eth_helper_functions.h"
#include "automaton/core/interop/ethereum/eth_transaction.h"
#include "automaton/core/io/io.h"
#include "automaton/tools/miner/miner.h"

using automaton::core::common::status;
using automaton::core::interop::ethereum::dec_to_32hex;
using automaton::core::interop::ethereum::eth_transaction;
using automaton::core::io::bin2hex;
using automaton::core::io::dec2hex;
using automaton::core::io::hex2bin;
using automaton::core::io::hex2dec;
using automaton::tools::miner::gen_pub_key;
using automaton::tools::miner::mine_key;
using automaton::tools::miner::sign;

using json = nlohmann::json;

// Ganache test
static const char* URL = "127.0.0.1:7545";
static const char* CONTRACT_ADDR = "0x4B7c78aB094Ab856eAdA523Fa847236567e1750E";
static const char* ADDRESS = "0x603CB0d1c8ab86E72beb3c7DF564A36D7B85ecD2";
static const char* PRIVATE_KEY = "56aac550d97013a8402c98e3b2aeb20482d19f142a67022d2ab357eb8bb673b0";

// static const char* CONTRACT_ADDR = "0x9de3744909Ba0587A988E10eE7F73960e224980F";
// static const char* ADDRESS = "0x2a9fe9D9b0dae89C48b8B8F4E008E17f1A1ED4A6";
// static const char* PRIVATE_KEY = "11937405a1975b68ff0e0fc7e3eedcf21e953113b35f95af30839b44b4960c99";

// Connect via CloudFlare
// static const char* URL = "https://cloudflare-eth.com/";
// static const char* CONTRACT_ADDR = "...";
// static const char* ADDRESS = "0x5a0b54d5dc17e0aadc383d2db43b0a0d3e029c4c";


static const char* JSON_FILE = "../contracts/koh/build/contracts/KingAutomaton.json";

// TODO(kari): Turn this file into an integration test.

void callback_func(const automaton::core::common::status& s, const std::string& response) {
  std::cout << "\n======= RESPONSE =======\n" << s << "\n" << response << "\n=====================\n\n";
}

int main() {
  std::string BIN_ADDRESS = hex2bin(std::string(24, '0') + std::string(ADDRESS+2));

  // std::string url, contract_addr;
  // std::cout << "Enter URL: ";
  // std::cin >> url;
  // std::cout << "Enter contract address: ";
  // std::cin >> contract_addr;

  std::unique_ptr<g3::LogWorker> logworker {g3::LogWorker::createLogWorker()};
  auto l_handler = logworker->addDefaultLogger("demo", "./");
  g3::initializeLogging(logworker.get());

  curl_global_init(CURL_GLOBAL_ALL);

  using namespace automaton::core::interop::ethereum;  // NOLINT

  std::fstream fs(JSON_FILE, std::fstream::in);
  json j, abi;
  fs >> j;
  if (j.find("abi") != j.end()) {
    abi = j["abi"].get<json>();
  } else {
    LOG(WARNING) << "No abi!";
    curl_global_cleanup();
    return 0;
  }
  std::string abi_string = abi.dump();
  eth_contract::register_contract(URL, CONTRACT_ADDR, abi_string);
  fs.close();

  auto contract = eth_contract::get_contract(CONTRACT_ADDR);
  if (contract == nullptr) {
    LOG(WARNING) << "Contract is NULL";
    curl_global_cleanup();
    return 0;
  }

  status s = status::ok();
  json j_output;

  std::cout << "Fetching Gas price..." << std::endl;
  s = eth_gasPrice(URL);
  if (s.code == automaton::core::common::status::OK) {
    std::cout << "Gas price: " << hex2dec(s.msg.substr(2)) << std::endl;
  } else {
    LOG(WARNING) << "Error (eth_gasPrice()) " << s << std::endl;
    curl_global_cleanup();
    return 0;
  }

  s = eth_getCode(URL, CONTRACT_ADDR);
  if (s.code == automaton::core::common::status::OK) {
    if (s.msg == "" || s.msg == "0x") {
      LOG(WARNING) << "Contract NOT FOUND!" << std::endl;
      curl_global_cleanup();
      return 0;
    } else {
      std::string m = s.msg.substr(2);
      std::cout << "Code: " << m.substr(0, std::min<size_t>(64, m.size())) << "..." << std::endl;
    }
  } else {
    std::cout << "Error (eth_getCode()) " << s << std::endl;
  }

  s = contract->call("numSlots", "");
  if (s.code == automaton::core::common::status::OK) {
    j_output = json::parse(s.msg);
    std::cout << "Number of slots: " << (*j_output.begin()).get<std::string>() << std::endl;
  } else {
    std::cout << "Error (numSlots()) " << s << std::endl;
  }

  s = contract->call("getSlotOwner", "[2]");
  if (s.code == automaton::core::common::status::OK) {
    j_output = json::parse(s.msg);
    std::cout << "Slot 2 owner: " << (*j_output.begin()).get<std::string>() << std::endl;
  } else {
    std::cout << "Error (getSlotOwner(2)) " << s << std::endl;
  }

  s = contract->call("getSlotDifficulty", "[2]");
  if (s.code == automaton::core::common::status::OK) {
    j_output = json::parse(s.msg);
    std::cout << "Slot 2 difficulty: " <<
        bin2hex(dec_to_i256(false, (*j_output.begin()).get<std::string>())) << std::endl;
  } else {
    std::cout << "Error (getSlotDifficulty(2)) " << s << std::endl;
  }

  s = contract->call("getSlotLastClaimTime", "[2]");
  if (s.code == automaton::core::common::status::OK) {
    j_output = json::parse(s.msg);
    std::cout << "Slot 2 last claim time: " << (*j_output.begin()).get<std::string>() << std::endl;
  } else {
    std::cout << "Error (getSlotLastClaimTime(2)) " << s << std::endl;
  }

  std::string hex_mask;
  s = contract->call("mask", "");
  if (s.code == automaton::core::common::status::OK) {
    j_output = json::parse(s.msg);
    hex_mask = bin2hex(dec_to_i256(false, (*j_output.begin()).get<std::string>()));
    std::cout << "Mask: " << hex_mask << std::endl;
  } else {
    std::cout << "Error (mask()) " << s << std::endl;
  }

  std::string hex_min_diff;
  s = contract->call("minDifficulty", "");
  if (s.code == automaton::core::common::status::OK) {
    j_output = json::parse(s.msg);
    hex_min_diff = bin2hex(dec_to_i256(false, (*j_output.begin()).get<std::string>()));
    std::cout << "Min Difficulty: " << hex_min_diff << std::endl;
  } else {
    std::cout << "Error (minDifficulty()) " << s << std::endl;
  }

  s = contract->call("numTakeOvers", "");
  if (s.code == automaton::core::common::status::OK) {
    j_output = json::parse(s.msg);
    std::cout << "Number of slot take overs: " << (*j_output.begin()).get<std::string>() << std::endl;
  } else {
    std::cout << "Error (numTakeOvers()) " << s << std::endl;
  }

  // Mine key.
  unsigned char mask[32];
  unsigned char difficulty[32];
  unsigned char pk[32];
  std::string bin_mask = hex2bin(hex_mask);
  memcpy(&mask, bin_mask.c_str(), 32);
  std::string bin_min_diff = hex2bin(hex_min_diff);
  memcpy(&difficulty, bin_min_diff.c_str(), 32);
  unsigned int keys_generated = mine_key(mask, difficulty, pk, 10000000);
  if (keys_generated == 0) {
    LOG(WARNING) << "Did not mine a key...";
    curl_global_cleanup();
    return 0;
  }

  // Generate signature.
  std::string pub_key = gen_pub_key(pk);
  std::string sig = sign(pk, reinterpret_cast<const unsigned char *>(BIN_ADDRESS.c_str()));

  // Encode claimSlot data.
  int32_t v = sig[64];
  std::stringstream claim_slot_params;
  claim_slot_params << "[\""
      // pubKeyX
      << bin2hex(pub_key.substr(0, 32)) << "\",\""
      // pubKeyY
      << bin2hex(pub_key.substr(32, 32)) << "\","
      // v
      << v << ",\""
      // r
      << bin2hex(sig.substr(0, 32)) << "\",\""
      // s
      << bin2hex(sig.substr(32, 32)) << "\"]";

  s = contract->call("claimSlot", claim_slot_params.str(), PRIVATE_KEY);
  if (s.code == automaton::core::common::status::OK) {
    std::cout << "Claim slot result: " << s.msg << std::endl;
  } else {
    std::cout << "Error (claimSlot) " << s << std::endl;
  }

  s = contract->call("numTakeOvers", "");
  if (s.code == automaton::core::common::status::OK) {
    j_output = json::parse(s.msg);
    std::cout << "Number of slot take overs: " << (*j_output.begin()).get<std::string>() << std::endl;
  } else {
    std::cout << "Error (numTakeOvers()) " << s << std::endl;
  }

  s = contract->call("createProposal",
  "[\"603CB0d1c8ab86E72beb3c7DF564A36D7B85ecD2\", \"title\",\"documents_link\",\"\",259200,3,20]",
  PRIVATE_KEY);
  if (s.code == automaton::core::common::status::OK) {
    std::cout << "Create proposal result: " << s.msg << std::endl;
  } else {
    std::cout << "Error (createProposal) " << s << std::endl;
  }

  // Mine key 2.
  keys_generated = mine_key(mask, difficulty, pk, 10000000);
  if (keys_generated == 0) {
    LOG(WARNING) << "Did not mine a key...";
    curl_global_cleanup();
    return 0;
  }

  // Generate signature.
  pub_key = gen_pub_key(pk);
  sig = sign(pk, reinterpret_cast<const unsigned char *>(BIN_ADDRESS.c_str()));

  uint32_t nonce = 0;
  s = eth_getTransactionCount(URL, ADDRESS);
  if (s.code == automaton::core::common::status::OK) {
    nonce = hex2dec(s.msg.substr(2));
    std::cout << "Nonce is: " << nonce << std::endl;
  } else {
    LOG(WARNING) << "Error (eth_getTransactionCount()) " << s;
    curl_global_cleanup();
    return 0;
  }

  // Encode claimSlot data.
  std::stringstream claim_slot_data;
  claim_slot_data
      // claimSlot signature
      << "6b2c8c48"
      // pubKeyX
      << bin2hex(pub_key.substr(0, 32))
      // pubKeyY
      << bin2hex(pub_key.substr(32, 32))
      // v
      << std::string(62, '0') << bin2hex(sig.substr(64, 1))
      // r
      << bin2hex(sig.substr(0, 32))
      // s
      << bin2hex(sig.substr(32, 32));

  eth_transaction t;
  t.nonce = dec2hex(nonce);
  t.gas_price = "1388";  // 5 000
  t.gas_limit = "5B8D80";  // 6M
  t.to = std::string(CONTRACT_ADDR + 2);
  t.value = "";
  t.data = claim_slot_data.str();
  t.chain_id = "01";

  s = contract->call("claimSlot", t.sign_tx(PRIVATE_KEY));  // Send raw transaction
  if (s.code == automaton::core::common::status::OK) {
    std::cout << "Claim slot result (raw tx): " << s.msg << std::endl;
  } else {
    std::cout << "Error (claimSlot raw) " << s << std::endl;
  }

  s = contract->call("numTakeOvers", "");
  if (s.code == automaton::core::common::status::OK) {
    j_output = json::parse(s.msg);
    std::cout << "Number of slot take overs: " << (*j_output.begin()).get<std::string>() << std::endl;
  } else {
    std::cout << "Error (numTakeOvers()) " << s << std::endl;
  }

  curl_global_cleanup();
  return 0;
}
