#include <iostream>
#include <fstream>
#include <string>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: setFan <0|1|2>\n";
        return 1;
    }

    string path;

    for (int i = 0; i < 20; i++) {
        path = "/sys/class/hwmon/hwmon" + to_string(i) + "/pwm1_enable";

        ofstream test(path, ios::app);
        if (test.is_open()) {
            test.close();
            break;
        }

        path.clear();
    }

    if (path.empty()) {
        cerr << "Fan control not found\n";
        return 1;
    }

    ofstream f(path);

    if (!f) {
        cerr << "Cannot open " << path << "\n";
        return 1;
    }

    f << argv[1];

    cout << "Fan set to " << argv[1] << "\n";

    return 0;
}