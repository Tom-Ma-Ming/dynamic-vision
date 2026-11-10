#include "vision/control/Assist.hpp"

#include <iostream>
#include <string>

namespace {

void usage() {
  std::cerr << "usage:\n"
               "  serial_assist inventory --root ROOT --out DIR --commit SHA --dirty 0|1\n"
               "  serial_assist run --root ROOT --config configs/serial/assist.json --out DIR --commit SHA --dirty 0|1\n"
               "\n"
               "serial is closed unless a later SA05 transport is added. This program does not\n"
               "scan ports, open a COM port, flash firmware, or tune PID gains.\n"
               "start-preview, start-simulation, start-serial-test and stop are not implemented.\n";
}

std::string arg(int argc, char** argv, const std::string& key, const std::string& fallback = "") {
  for (int i = 0; i + 1 < argc; ++i) {
    if (argv[i] == key) return argv[i + 1];
  }
  return fallback;
}

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string command = argv[1];
  if (command == "--help" || command == "-h" || command == "help") {
    usage();
    return 0;
  }
  if (command == "start-preview" || command == "start-simulation" || command == "start-serial-test" ||
      command == "stop") {
    std::cerr << command << " is not implemented. SA07 owns that package; this run did not execute it.\n";
    return 2;
  }
  vision::serial::SuitePaths paths;
  paths.root = arg(argc, argv, "--root", ".");
  paths.output = arg(argc, argv, "--out");
  paths.config = arg(argc, argv, "--config", "configs/serial/assist.json");
  if (paths.config.is_relative()) paths.config = paths.root / paths.config;
  paths.commit = arg(argc, argv, "--commit");
  paths.dirty = arg(argc, argv, "--dirty", "1") != "0";
  for (int i = 0; i < argc; ++i) {
    if (i) paths.launch += " ";
    paths.launch += argv[i];
  }
  if (paths.output.empty()) {
    usage();
    return 2;
  }
  if (command == "inventory") return vision::serial::runInventory(paths);
  if (command == "run") return vision::serial::runSuite(paths);
  usage();
  return 2;
} catch (const std::exception& error) {
  std::cerr << error.what() << "\n";
  return 1;
}
