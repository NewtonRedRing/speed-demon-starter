#include <iostream>
#include <stdexcept>
#include "lab.hpp"

int main(int argc, char** argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("usage: speeddemon_b input output");
    lab::index(argv[1], argv[2], argv[2]);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "speeddemon_b: " << error.what() << '\n';
    return 1;
  }
}
