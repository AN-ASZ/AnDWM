#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

static constexpr auto BATTERY_CAPACITY_PATH = "/sys/class/power_supply/BAT0/capacity";
static constexpr auto BATTERY_STATUS_PATH   = "/sys/class/power_supply/BAT0/status";

// Read battery percentage; returns -1 on error
int read_battery_level() {
    std::ifstream in(BATTERY_CAPACITY_PATH);
    if (!in) return -1;

    std::string s;
    if (!std::getline(in, s)) return -1;

    try {
        return std::stoi(s);
    } catch (...) {
        return -1;
    }
}

// Check if battery is charging
bool is_charging() {
    std::ifstream in(BATTERY_STATUS_PATH);
    if (!in) return false;

    std::string status;
    if (!std::getline(in, status)) return false;

    // Remove trailing whitespace
    while (!status.empty() && (status.back() == '\n' || status.back() == '\r'))
        status.pop_back();

    return status == "Charging" || status == "Full";
}

// Show notification using notify-send
void notify(const std::string &title, const std::string &message, int timeout_ms = 5000) {
    std::string cmd = "notify-send -u critical -t " + std::to_string(timeout_ms) +
                      " \"" + title + "\" \"" + message + "\"";
    std::system(cmd.c_str());
}

class BatteryMonitor {
public:
    int run() {
        while (true) {
            int level = read_battery_level();
            if (level < 0) {
                std::cerr << "Failed to read battery level\n";
                std::this_thread::sleep_for(std::chrono::minutes(1));
                continue;
            }

            bool charging = is_charging();

            // Battery low notification only if not charging
            if (level <= 5 && level > 2 && !charging) {
                notify("Battery Low", "Battery is below 5%! Please plug in.", 10000);
                std::this_thread::sleep_for(std::chrono::seconds(300)); // avoid spamming
            }

            // Battery critical countdown (≤2% and not charging)
            if (level <= 2 && !charging) {
                int countdown = 5; // seconds
                notify("Battery Critical",
                       "Battery below 2%! Shutting down in 5 seconds unless charging.",
                       5000);

                while (countdown > 0) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    if (is_charging()) {
                        notify("Charging Detected", "Charging detected. Shutdown canceled.", 3000);
                        break;
                    }
                    --countdown;
                }

                if (countdown == 0) {
                    std::system("systemctl poweroff");
                    return 0;
                }
            }

            std::this_thread::sleep_for(std::chrono::seconds(300)); // check every 5 minutes
        }

        return 0;
    }
};

int main() {
    BatteryMonitor monitor;
    return monitor.run();
}
