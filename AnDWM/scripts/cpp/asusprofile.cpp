#include <cstdlib>
#include <thread>
#include <chrono>

int main() {
    while (true) {
        // If "Performance" is NOT active, set it
        if (system("asusctl profile get | grep -iq \"Active profile: Performance\"") == 0) {
            system("asusctl profile set Performance >/dev/null 2>&1");
        }

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}