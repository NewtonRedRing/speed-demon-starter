#include "runtime.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

void require(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}
template<class Action>
void expect_invalid(Action action) {
    bool failed = false;
    try { action(); }
    catch (const std::invalid_argument&) { failed = true; }
    require(failed, "an invalid value was accepted");
}
int main() {
    try {
        require(hpc::parse_threads("1") == 1, "min thread count");
        require(hpc::parse_threads("256") == 256, "max thread count");
        expect_invalid([] { hpc::parse_threads("0"); });
        expect_invalid([] { hpc::parse_threads("-1"); });
        expect_invalid([] { hpc::parse_threads("257"); });
        expect_invalid([] { hpc::parse_threads("2a"); });
        expect_invalid([] { hpc::parse_threads(""); });
        char name[] = "runtime_unit";
        char input[] = "--input-dir";
        char data[] = "fixtures";
        char output[] = "--output-dir";
        char dest[] = "runtime-test-output";
        char threads[] = "--threads";
        char count[] = "3";
        char* args[] = {name, input, data, output, dest, threads, count};
        hpc::configure(7, args);
        require(hpc::input_path("requests.log") == "fixtures/requests.log", "input path");
        require(hpc::output_path("summary.txt") == "runtime-test-output/summary.txt", "output path");
        require(hpc::worker_count(12) == 3, "worker cap");
        require(hpc::worker_count(1) == 1, "single worker");
        require(std::filesystem::is_directory("runtime-test-output"), "output directory");
        std::filesystem::remove_all("runtime-test-output");
        std::cout << "runtime unit checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
