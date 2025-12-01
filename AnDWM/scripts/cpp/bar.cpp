#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <sstream>
#include <array>
#include <memory>
#include <unistd.h>
#include <algorithm>
#include <vector>
#include <cstdio>

std::string black="#1E1D2D";
std::string green="#a6e3a1";
std::string white="#D9E0EE";
std::string grey="#282737";
std::string blue="#96CDFB";
std::string red="#fe640b";
std::string darkblue="#83bae8";
std::string teal="#a6da95";
std::string green2="#a6da95";
std::string teal2="#5BA643";
std::string green3="#B8E9B4";

int g_scroll_index = 0;

// Utility function to execute shell command and capture output
std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}

// Utility function to trim whitespace
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Format artist: uppercase + remove " - TOPIC"
std::string formatArtist(const std::string& artist) {
    std::string res = artist;
    std::transform(res.begin(), res.end(), res.begin(), ::toupper);
    std::string suffix = " - TOPIC";
    if (res.size() >= suffix.size() &&
        res.compare(res.size() - suffix.size(), suffix.size(), suffix) == 0) {
        res.erase(res.size() - suffix.size());
    }
    return res;
}

// Split UTF-8 string into characters
std::vector<std::string> utf8ToChars(const std::string& str) {
    std::vector<std::string> chars;
    for (size_t i = 0; i < str.size();) {
        unsigned char c = str[i];
        size_t char_len = 1;
        if ((c & 0x80) == 0) char_len = 1;
        else if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;
        chars.push_back(str.substr(i, char_len));
        i += char_len;
    }
    return chars;
}

// Get display width of a UTF-8 character (East Asian Width aware)
// Returns 0 for combining characters (Thai vowels/tones, accents), 
// 2 for wide characters (CJK), 1 for normal characters
int getCharWidth(const std::string& utf8char) {
    if (utf8char.length() < 2) return 1; // ASCII
    
    unsigned char c1 = utf8char[0];
    unsigned char c2 = utf8char[1];
    unsigned char c3 = (utf8char.length() > 2) ? utf8char[2] : 0;
    
    // Thai combining characters and vowels (E0 B8 - E0 B9 range)
    // ี ่ ์ ๊ ๋ ํ ุ ู and other Thai diacritics/vowels
    if (c1 == 0xE0 && (c2 == 0xB8 || c2 == 0xB9)) {
        // Thai block - check for combining marks
        // Thai vowels above: ิ ี ึ ื (0xB8 0xB4-0xB7)
        // Thai tone marks: ่ ้ ๊ ๋ (0xB9 0x88-0x8B)
        // Thai vowel below: ั ุ ู (0xB8 0xB1-0xB3)
        // Thai mai kham: ํ (0xB8 0xB3)
        if ((c2 == 0xB8 && (c3 >= 0xB1 && c3 <= 0xB7)) ||
            (c2 == 0xB9 && (c3 >= 0x88 && c3 <= 0x8B))) {
            return 0; // Combining character - zero width
        }
    }
    
    // General Unicode combining marks and diacritics (U+0300 to U+036F)
    if (c1 == 0xCC || (c1 == 0xCD && c2 <= 0xAF)) {
        return 0; // Zero width combining marks
    }
    
    // Zero Width Joiner, Zero Width Non-Joiner, etc.
    if (c1 == 0xE2 && c2 == 0x80 && (c3 == 0x8C || c3 == 0x8D)) {
        return 0;
    }
    
    // CJK Unified Ideographs (Chinese/Japanese/Korean characters)
    if (c1 == 0xE4 || c1 == 0xE5 || c1 == 0xE6 || c1 == 0xE7 || c1 == 0xE8 || 
        c1 == 0xE9 || c1 == 0xEA || c1 == 0xEB) {
        return 2;
    }
    
    // CJK Unified Ideographs Extension
    if (c1 == 0xF0 && (c2 == 0xA0 || c2 == 0xA1 || c2 == 0xA2 || c2 == 0xA3 || 
                       c2 == 0xA4 || c2 == 0xA5 || c2 == 0xA6 || c2 == 0xA7)) {
        return 2;
    }
    
    // Hiragana and Katakana
    if (c1 == 0xE3 && (c2 == 0x81 || c2 == 0x82 || c2 == 0x83)) {
        return 2;
    }
    
    return 1; // Default single width
}

// Calculate total display width of a string
int getStringWidth(const std::vector<std::string>& chars) {
    int width = 0;
    for (const auto& ch : chars) {
        width += getCharWidth(ch);
    }
    return width;
}

// Read file content
std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return trim(content);
}

std::string battery() {
    std::string capacity_str = read_file("/sys/class/power_supply/BAT0/capacity");
    std::string charging_str = read_file("/sys/class/power_supply/AC0/online");
    
    if (capacity_str.empty() || charging_str.empty()) {
        return "";
    }
    
    int capacity = std::stoi(capacity_str);
    int charging = std::stoi(charging_str);
    std::string icon;
    
    if (charging == 1) {
        icon = "󰂄 ";
    } else if (capacity >= 90) {
        icon = "󰁹 ";
    } else if (capacity >= 80) {
        icon = "󰂂 ";
    } else if (capacity >= 70) {
        icon = "󰂁 ";
    } else if (capacity >= 60) {
        icon = "󰂀 ";
    } else if (capacity >= 50) {
        icon = "󰁿 ";
    } else if (capacity >= 40) {
        icon = "󰁾 ";
    } else if (capacity >= 30) {
        icon = "󰁽 ";
    } else if (capacity >= 20) {
        icon = "󰁼 ";
    } else if (capacity >= 10) {
        icon = "󰁻 ";
    } else {
        icon = "󰁺 ";
    }
    
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "^c%s^%s%d%%", blue.c_str(), icon.c_str(), capacity);
    return std::string(buffer);
}

std::string clock_time() {
    std::time_t now = std::time(nullptr);
    std::tm* timeinfo = std::localtime(&now);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M", timeinfo);
    
    std::string result = "^c" + black + "^ ^b" + darkblue + "^ 󱑆 ";
    result += "^c" + black + "^^b" + blue + "^ " + std::string(buffer) + " ";
    result += "^d^" + ("^c" + blue + "^");
    return result;
}

std::string kb_layout() {
    std::string layout = trim(exec("xkblayout-state print \"%s\" | tr '[:lower:]' '[:upper:]'"));
    if (layout.empty()) {
        return "";
    }
    
    std::string result = "^c" + black + "^ ^b" + blue + "^ " + layout + " ";
    result += "^d^" + ("^c" + blue + "^");
    return result;
}

std::string get_playing_player() {
    std::string players = trim(exec("playerctl -l 2>/dev/null"));
    if (players.empty()) {
        return "";
    }
    
    std::istringstream iss(players);
    std::string player;
    while (std::getline(iss, player)) {
        player = trim(player);
        if (player.empty()) continue;
        
        std::string cmd = "playerctl -p " + player + " status 2>/dev/null";
        std::string status = trim(exec(cmd.c_str()));
        if (status == "Playing") {
            return player;
        }
    }
    return "";
}

std::string player_info(const std::string& app) {
    // Colors
    std::string black = "#1E1D2D";
    std::string green2 = "#a6da95";
    std::string green3 = "#B8E9B4";
    std::string blue = "#96CDFB";

    // Artist + Title
    std::string rawArtist = trim(exec(("playerctl -p " + app + " metadata --format '{{ artist }}'").c_str()));
    std::string ARTIST = formatArtist(rawArtist);
    std::string TITLE = trim(exec(("playerctl -p " + app + " metadata --format '{{ title }}'").c_str())) + " ";
    std::string TEXT = !ARTIST.empty() ? "[" + ARTIST + "]  " + TITLE : TITLE;

    int SCROLL_WIDTH = 30;

    // ===== Scroll index now global =====
    int& i = g_scroll_index;     // reference → modifies global
    // ===================================

    // UTF-8 safe processing
    std::vector<std::string> chars = utf8ToChars(TEXT);
    int TEXT_LEN = chars.size();
    if (i >= TEXT_LEN) i = 0;

    // Duplicate for looping animation
    std::vector<std::string> SCROLL_CHARS = chars;
    SCROLL_CHARS.insert(SCROLL_CHARS.end(), chars.begin(), chars.end());

    // Playback
    double pos = 0.0, len = 1.0;
    std::string posStr = trim(exec(("playerctl -p " + app + " position").c_str()));
    std::string lenStr = trim(exec(("playerctl -p " + app + " metadata mpris:length").c_str()));
    if (!posStr.empty()) pos = std::stod(posStr);
    if (!lenStr.empty()) len = std::stod(lenStr) / 1000000.0;

    std::string SLICE_BEFORE, SLICE_AFTER, LAST_BLOCK;
    double mapped;
    //calculate percentage 0-90%
    double percent = (pos / len) * 100.0;
    
    // Adjust scroll width if text shorter than bar
    int textWidth = getStringWidth(chars);
    if (textWidth <= SCROLL_WIDTH) {
        SCROLL_WIDTH = textWidth; // use actual visual width
        i = 0;
        mapped = std::min(percent / 0.95, 100.0);  // map 0–85% to 0–100%
    } else {
        mapped = std::min(percent / 0.82, 100.0);  // map 0–90% to 0–100%
        if (percent >= 95) {
            LAST_BLOCK = "^c" + green2 + "^^b" + green2 + "^ ";
        } else {
            LAST_BLOCK = " ";
        }
        
    }
    
    int progress_width = static_cast<int>((mapped / 100.0) * SCROLL_WIDTH);

    // Build slices
    int current_width = 0;
    int j = 0;



    while (current_width < progress_width && j < (int)SCROLL_CHARS.size()) {
        int char_width = getCharWidth(SCROLL_CHARS[(i + j) % SCROLL_CHARS.size()]);
        if (current_width + char_width <= progress_width) {
            SLICE_BEFORE += SCROLL_CHARS[(i + j) % SCROLL_CHARS.size()];
            current_width += char_width;
            j++;
        } else break;
    }

    int start_j = j;
    while (current_width < SCROLL_WIDTH && j < start_j + (int)SCROLL_CHARS.size()) {
        int char_width = getCharWidth(SCROLL_CHARS[(i + j) % SCROLL_CHARS.size()]);
        if (current_width + char_width <= SCROLL_WIDTH) {
            SLICE_AFTER += SCROLL_CHARS[(i + j) % SCROLL_CHARS.size()];
            current_width += char_width;
            j++;
        } else break;
    }

    // Build bar
    std::string output =
          "^c" + black + "^ ^b" + green2 + "^ 󰎆 "
        + "^c" + black + "^" + SLICE_BEFORE
        + "^b" + green3 + "^" + SLICE_AFTER + LAST_BLOCK
        + "^d^" + "^c" + blue + "^";

    // Scroll for next tick
    i = (i + 1) % TEXT_LEN;

    return output;
}


std::string info_play() {
    // Get memory info
    std::string free_output = exec("free -h | awk '/^Mem/ {used=$3; gsub(/i/,\"\",used); printf \"%4.1f\", used}'");
    std::string mem_unit = trim(exec("free -h | awk '/^Mem/ {print $3}' | sed 's/[0-9.]*//g'"));
    std::string mem_percent_str = trim(exec("free -m | awk '/^Mem/ {printf(\"%d\", $3*100/$2)}'"));
    
    int mem_percent = mem_percent_str.empty() ? 0 : std::stoi(mem_percent_str);
    
    std::string icon, t_color;
    if (mem_percent < 40) {
        icon = "󰾆";
        t_color = black;
    } else if (mem_percent < 80) {
        icon = "󰾅";
        t_color = black;
    } else {
        icon = "󰓅";
        t_color = red;
    }
    
    // Get WiFi info
    std::string interface = trim(exec("iw dev | awk '$1==\"Interface\"{print $2; exit}'"));
    std::string strength_str = trim(exec(("iw dev " + interface + " link | awk '/signal/ {print int($2)}'").c_str()));
    int strength = strength_str.empty() ? -100 : std::stoi(strength_str);
    
    std::string wicon;
    if (strength > -55) {
        wicon = "󰤨";
    } else if (strength > -85) {
        wicon = "󰤥";
    } else if (strength > -90) {
        wicon = "󰤢";
    } else if (strength < -90) {
        wicon = "󰤟";
    } else {
        wicon = "󰤭";
    }
    
    std::string state = read_file("/sys/class/net/" + interface + "/operstate");
    state = trim(state);
    
    // Check for playing media
    std::string player = get_playing_player();
    std::string result;
    
    if (!player.empty()) {
        // Call player_info() inline instead of external script
        std::string player_output = player_info(player);
        
        result = player_output + "^c" + t_color + "^ ^b" + blue + "^ " + icon + " ^d^";
        if (state == "up") {
            result += "^c" + black + "^ ^b" + blue + "^ " + wicon + " ^d^";
        } else {
            result += "^c" + black + "^ ^b" + blue + "^ 󰤭^d^";
        }
    } else {
        // Write scroll position and show CPU
        g_scroll_index = 0;

        
        // Get CPU info
        std::string cpu_str = trim(exec("top -bn1 | awk '/Cpu\\(s\\)/ {usage = 100 - $8; printf \"%4.1f\", usage}'"));
        
        result = "^c" + black + "^ ^b" + green + "^ ";
        result += "^c" + white + "^ ^b" + grey + "^ " + cpu_str + "% ^d^";
        result += "^c" + t_color + "^ ^b" + green + "^ " + icon;
        result += "^c" + white + "^ ^b" + grey + "^ " + trim(free_output) + mem_unit + " ^d^";
        
        std::string get_wifi = trim(exec("iwgetid -r"));
        if (state == "up") {
            result += "^c" + black + "^ ^b" + blue + "^ " + wicon + "^c" + white + "^ ^b" + grey + "^ " + get_wifi + " ^d^";
        } else {
            result += "^c" + black + "^ ^b" + blue + "^ 󰤭^c" + white + "^ ^b" + grey + "^ Disconnected ^d^";
        }
    }
    
    return result;
}

int main() {
    int interval = 0;
    
    while (true) {
        if (interval == 0 || (interval % 3600) == 0) {
            interval = interval + 1;
        }
        
        usleep(700000);
        
        std::string bar_output = "   " + battery() + info_play() + clock_time() + kb_layout() + "^b" + black + "^ ";
        std::string xsetroot_cmd = "xsetroot -name \"" + bar_output + "\"";
        system(xsetroot_cmd.c_str());
    }
    
    return 0;
}
