#include <iostream>
#include <stdexcept>
#include <filesystem>
#include "graph.hpp"

int main(int argc, char** argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("usage: speeddemon_d input");
    
    std::filesystem::path inputPath(argv[1]);
    std::filesystem::path outputPath = inputPath.parent_path() / "distances.txt";
    
    practice::solveGraph(inputPath, outputPath);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "speeddemon_d: " << error.what() << '\n';
    return 1;
  }
}
