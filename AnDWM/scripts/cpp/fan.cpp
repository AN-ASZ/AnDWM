#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <string>
#include <glob.h>

using namespace std;

string find_pwm1_enable() {
    const char* pattern =
        "/sys/devices/platform/asus-nb-wmi/hwmon/hwmon*/device/hwmon/hwmon*/pwm1_enable";

    glob_t g{};
    std::string result;

    if (glob(pattern, 0, nullptr, &g) == 0 && g.gl_pathc > 0) {
        result = g.gl_pathv[0];
    }

    globfree(&g);
    return result;
}

int main(int argc, char* argv[]) {
    if (argc != 2 || (std::string(argv[1]) != "0" && std::string(argv[1]) != "2")) {
        std::cerr << "Usage: " << argv[0] << " [0|2]\n";
        return 1;
    }

    string path = find_pwm1_enable();

    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open " << path << "\n";
        return 1;
    }

    file << argv[1];
    file.close();

    std::cout << "Fan control set to: " << argv[1] << "\n";
    return 0;
}