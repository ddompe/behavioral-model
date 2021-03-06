/* Copyright 2013-present Barefoot Networks, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

#include <bm/bm_sim/options_parse.h>
#include <bm/bm_sim/event_logger.h>

#include <boost/program_options.hpp>

#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include <cassert>

namespace bm {

struct interface {
  interface(const std::string &name, int port)
    : name(name), port(port) { }

  std::string name{};
  int port{};
};

void validate(boost::any& v,  // NOLINT(runtime/references)
              const std::vector<std::string> &values,
              interface* /* target_type */,
              int) {
  namespace po = boost::program_options;

  // Make sure no previous assignment to 'v' was made.
  po::validators::check_first_occurrence(v);

  // Extract the first string from 'values'. If there is more than
  // one string, it's an error, and exception will be thrown.
  const std::string &s = po::validators::get_single_string(values);

  std::istringstream stream(s);
  std::string tok;
  std::getline(stream, tok, '@');
  int port;
  try {
    port = std::stoi(tok, nullptr);
  }
  catch (...) {
    throw po::validation_error(po::validation_error::invalid_option_value,
                               "interface");
  }
  if (tok == s) {
    throw po::validation_error(po::validation_error::invalid_option_value,
                               "interface");
  }
  std::getline(stream, tok);
  v = boost::any(interface(tok, port));
}

void
OptionsParser::parse(int argc, char *argv[]) {
  namespace po = boost::program_options;

  po::options_description description("Options");

  description.add_options()
      ("help,h", "Display this help message")
      ("interface,i", po::value<std::vector<interface> >()->composing(),
       "<port-num>@<interface-name>: "
       "Attach network interface <interface-name> as port <port-num> at "
       "startup. Can appear multiple times")
      ("pcap", "Generate pcap files for interfaces")
      ("use-files", po::value<int>(), "Read/write packets from files "
       "(interface X corresponds to two files X_in.pcap and X_out.pcap).  "
       "Argument is the time to wait (in seconds) before starting to process "
       "the packet files.")
      ("packet-in", po::value<std::string>(),
       "Enable receiving packet on this (nanomsg) socket. "
       "The --interface options will be ignored.")
      ("thrift-port", po::value<int>(),
       "TCP port on which to run the Thrift runtime server")
      ("device-id", po::value<int>(),
       "Device ID, used to identify the device in IPC messages (default 0)")
      ("nanolog", po::value<std::string>(),
       "IPC socket to use for nanomsg pub/sub logs "
       "(default: no nanomsg logging")
      ("log-console",
       "Enable logging on stdout")
      ("log-file", po::value<std::string>(),
       "Enable logging to given file")
      ("log-level,L", po::value<std::string>(),
       "Set log level, supported values are "
       "'trace', 'debug', 'info', 'warn', 'error', off'")
      ("notifications-addr", po::value<std::string>(),
       "Specify the nanomsg address to use for notifications "
       "(e.g. learning, ageing, ...); "
       "default is ipc:///tmp/bmv2-<device-id>-notifications.ipc")
#ifdef BMDEBUG_ON
      ("debugger", "Activate debugger")
      ("debugger-addr", po::value<std::string>(),
       "Specify the nanomsg address to use for debugger communication; "
       "there is no need to use --debugger in addition to this option; "
       "default is ipc:///tmp/bmv2-<device-id>-debug.ipc")
#endif
      ("restore-state", po::value<std::string>(),
       "Restore state from file")
      ;  // NOLINT(whitespace/semicolon)

  po::options_description hidden;
  hidden.add_options()
    ("input-config", po::value<std::string>(), "input config");

  po::options_description options;
  options.add(description).add(hidden);

  po::positional_options_description positional;
  positional.add("input-config", 1);

  po::variables_map vm;
  try {
    po::store(po::command_line_parser(argc, argv).
              options(options).
              positional(positional).run(), vm);
    po::notify(vm);
  }
  catch(...) {
    std::cout << "Error while parsing command line arguments\n";
    std::cout << "Usage: SWITCH_NAME [options] <path to JSON config file>\n";
    std::cout << description;
    exit(1);
  }

  if (vm.count("help")) {
    std::cout << "Usage: SWITCH_NAME [options] <path to JSON config file>\n";
    std::cout << description;
    exit(0);
  }

  if (!vm.count("input-config")) {
    std::cout << "Error: please specify an input JSON configuration file\n";
    std::cout << "Usage: SWITCH_NAME [options] <path to JSON config file>\n";
    std::cout << description;
    exit(1);
  }

  device_id = 0;
  if (vm.count("device-id")) {
    device_id = vm["device-id"].as<int>();
  }

  if (vm.count("notifications-addr")) {
    notifications_addr = vm["notifications-addr"].as<std::string>();
  } else {
    notifications_addr = std::string("ipc:///tmp/bmv2-")
        + std::to_string(device_id) + std::string("-notifications.ipc");
  }

  if (vm.count("nanolog")) {
#ifndef BMELOG_ON
    std::cout << "Warning: you requested the nanomsg event logger, but bmv2 "
              << "was compiled without -DBMELOG, and the event logger cannot "
              << "be activated\n";
#else
    event_logger_addr = vm["nanolog"].as<std::string>();
    auto event_transport = TransportIface::make_nanomsg(event_logger_addr);
    event_transport->open();
    EventLogger::init(std::move(event_transport), device_id);
#endif
  }

  if (vm.count("log-console") && vm.count("log-file")) {
    std::cout << "Error: --log-console and --log-file are exclusive\n";
    exit(1);
  }

  if (vm.count("log-console")) {
    console_logging = true;
  }

  if (vm.count("log-file")) {
    file_logger = vm["log-file"].as<std::string>();
  }

  if (vm.count("log-level")) {
    const std::string log_level_str = vm["log-level"].as<std::string>();
    std::unordered_map<std::string, Logger::LogLevel> levels_map = {
      {"trace", Logger::LogLevel::TRACE},
      {"debug", Logger::LogLevel::DEBUG},
      {"info", Logger::LogLevel::INFO},
      {"warn", Logger::LogLevel::WARN},
      {"error", Logger::LogLevel::ERROR},
      {"off", Logger::LogLevel::OFF} };
    if (!levels_map.count(log_level_str)) {
      std::cout << "Invalid value " << log_level_str << " for --log-level\n"
                << "Run with -h to see possible values\n";
      exit(1);
    }
    log_level = levels_map[log_level_str];
  }

  if (vm.count("interface")) {
    for (const auto &iface : vm["interface"].as<std::vector<interface> >()) {
      ifaces.add(iface.port, iface.name);
    }
  }

  if (vm.count("pcap")) {
    pcap = true;
  }

  if (vm.count("use-files")) {
    use_files = true;
    wait_time = vm["use-files"].as<int>();
    if (wait_time < 0)
      wait_time = 0;
  }

  if (vm.count("packet-in")) {
    packet_in = true;
    packet_in_addr = vm["packet-in"].as<std::string>();
    // very important to clear interface list
    ifaces.clear();
  }

  if (use_files && packet_in) {
    std::cout << "Error: --use-files and --packet-in are exclusive\n";
    exit(1);
  }

  if (vm.count("debugger-addr")) {
    debugger = true;
    debugger_addr = vm["debugger-addr"].as<std::string>();
  } else if (vm.count("debugger")) {
    debugger = true;
    debugger_addr = std::string("ipc:///tmp/bmv2-")
        + std::to_string(device_id) + std::string("-debug.ipc");
  }

  assert(vm.count("input-config"));
  config_file_path = vm["input-config"].as<std::string>();

  int default_thrift_port = 9090;
  if (vm.count("thrift-port")) {
    thrift_port = vm["thrift-port"].as<int>();
  } else {
    std::cout << "Thrift port was not specified, will use "
              << default_thrift_port
              << std::endl;
    thrift_port = default_thrift_port;
  }

  if (vm.count("restore-state")) {
    state_file_path = vm["restore-state"].as<std::string>();
  }
}

}  // namespace bm
