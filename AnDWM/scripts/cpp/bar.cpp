#include <algorithm>
#include <array>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <sdbus-c++/sdbus-c++.h>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

// --- Thread Pool ---
class ThreadPool {
public:
  ThreadPool(size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i)
      workers.emplace_back([this] {
        for (;;) {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(this->queue_mutex);
            this->condition.wait(
                lock, [this] { return this->stop || !this->tasks.empty(); });
            if (this->stop && this->tasks.empty())
              return;
            task = std::move(this->tasks.front());
            this->tasks.pop();
          }
          task();
        }
      });
  }

  template <class F, class... Args>
  auto enqueue(F &&f, Args &&...args)
      -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      if (stop)
        throw std::runtime_error("enqueue on stopped ThreadPool");
      tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
  }

  ~ThreadPool() {
    {
      std::unique_lock<std::mutex> lock(queue_mutex);
      stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers)
      worker.join();
  }

private:
  std::vector<std::thread> workers;
  std::queue<std::function<void()>> tasks;
  std::mutex queue_mutex;
  std::condition_variable condition;
  bool stop;
};

// Global thread pool instance
static ThreadPool pool(4);

// Dim manager (foe some media app that dont prevent dimming, zen, etc..)
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/scrnsaver.h>

using namespace std;

// --- Global Colors ---
string black = "#1E1D2D";
string green = "#a6e3a1";
string white = "#D9E0EE";
string grey = "#282737";
string blue = "#96CDFB";
string red = "#fe640b";
string darkblue = "#83bae8";
string teal = "#a6da95";
string green2 = "#a6da95";
string teal2 = "#5BA643";
string green3 = "#B8E9B4";


// Variable for media scroll in bar
int g_scroll_index = 0;

// Variable for store dimming data
static Display *dpy = nullptr;
bool DimMode = 0;

// --- DBus Connections ---
sdbus::IConnection &getBus() {
  static auto connection = sdbus::createSessionBusConnection();
  return *connection;
}

sdbus::IConnection &getSystemBus() {
  static auto connection = sdbus::createSystemBusConnection();
  return *connection;
}

// --- Utility Functions ---
Display *getDisplay() {
  static Display *d = nullptr;

  if (!d) {
    d = XOpenDisplay(nullptr);
    if (!d) {
      fprintf(stderr, "Cannot open display\n");
      exit(1);
    }
  }

  return d;
}

static string findFanPath() {
    const string base = "/sys/devices/platform/asus-nb-wmi/hwmon/";
    for (const auto& entry : filesystem::recursive_directory_iterator(base)) {
        if (entry.is_regular_file() && entry.path().filename().string() == "pwm1_enable") {
            return entry.path().parent_path().string();
        }
    }
    return "";
}

string pwmPath;
string fanPath;
void setFan(const string& value) {
    fanPath = findFanPath();
    if (fanPath.empty()) {
        cerr << "Failed to find fan control path\n";
        return;
    }
    pwmPath = fanPath + "/pwm1_enable";
    ofstream f(pwmPath);
    if (!f.is_open()) {
        cerr << "Failed to open " << pwmPath << "\n";
        return;
    }
    f << value;
    f.close();
}

bool read_tmp(const string path) {
  std::ifstream f(path);

  if (!f.is_open()) {
    return false;
  }

  int value = 0;
  if (!(f >> value)) {
    return false;
  }

  return value != 0;
}

bool read_fullscreen() {
  return read_tmp("/tmp/isfullscreen");
}

string trim(const string &str) {
  size_t first = str.find_first_not_of(" \t\n\r");
  if (first == string::npos)
    return "";
  size_t last = str.find_last_not_of(" \t\n\r");
  return str.substr(first, (last - first + 1));
}

string exec(const char *cmd) {
  array<char, 128> buffer;
  string result;
  FILE *pipe = popen(cmd, "r");
  if (!pipe)
    return "";
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }
  pclose(pipe);
  return result;
}

string read_file(const string &path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0)
    return "";
  char buf[256];
  ssize_t n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return "";
  buf[n] = '\0';
  string s(buf);
  if (!s.empty() && s.back() == '\n')
    s.pop_back();
  return s;
}

string escape_quotes(string s) {
  string res;
  for (char c : s) {
    if (c == '"')
      res += "\\\"";
    else
      res += c;
  }
  return res;
}

// Prevent Dim Fcuntion
void setAutoDimTimeout(Display *dpy, int timeout) {
    if (!dpy)
        return;

    int current_timeout;
    int interval;
    int prefer_blanking;
    int allow_exposures;

    // Get current X11 screensaver settings
    XGetScreenSaver(
        dpy,
        &current_timeout,
        &interval,
        &prefer_blanking,
        &allow_exposures
    );

    // Set screensaver timeout
    XSetScreenSaver(
        dpy,
        timeout,
        interval,
        prefer_blanking,
        allow_exposures
    );

    // Disable DPMS (monitor power saving / dimming)
    int event_base, error_base;

    if (DPMSQueryExtension(dpy, &event_base, &error_base)) {
        CARD16 power_level;
        BOOL state;

        if (DPMSInfo(dpy, &power_level, &state) && state) {
            DPMSDisable(dpy);
        }
    }

    XFlush(dpy);
}

// --- NetworkManager DBus Logic ---

struct WifiStatus {
  string ssid = "Disconnected";
  int strength = 0;
  bool is_up = false;
};

WifiStatus get_wifi_info() {
  WifiStatus status;
  try {
    auto &connection = getSystemBus();
    auto nmProxy = sdbus::createProxy(
        connection, sdbus::ServiceName{"org.freedesktop.NetworkManager"},
        sdbus::ObjectPath{"/org/freedesktop/NetworkManager"});

    sdbus::Variant vActiveConn;
    nmProxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.freedesktop.NetworkManager", "PrimaryConnection")
        .storeResultsTo(vActiveConn);
    auto activeConnPath = vActiveConn.get<sdbus::ObjectPath>();

    if (activeConnPath.empty() || activeConnPath == "/")
      return status;

    auto connProxy = sdbus::createProxy(
        connection, sdbus::ServiceName{"org.freedesktop.NetworkManager"},
        activeConnPath);
    sdbus::Variant vDevices;
    connProxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.freedesktop.NetworkManager.Connection.Active",
                       "Devices")
        .storeResultsTo(vDevices);
    auto devices = vDevices.get<vector<sdbus::ObjectPath>>();

    if (devices.empty())
      return status;

    auto deviceProxy = sdbus::createProxy(
        connection, sdbus::ServiceName{"org.freedesktop.NetworkManager"},
        devices[0]);
    sdbus::Variant vDeviceType;
    deviceProxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.freedesktop.NetworkManager.Device", "DeviceType")
        .storeResultsTo(vDeviceType);

    if (vDeviceType.get<uint32_t>() == 2) { // Wi-Fi
      status.is_up = true;
      sdbus::Variant vApPath;
      deviceProxy->callMethod("Get")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments("org.freedesktop.NetworkManager.Device.Wireless",
                         "ActiveAccessPoint")
          .storeResultsTo(vApPath);
      auto apPath = vApPath.get<sdbus::ObjectPath>();

      auto apProxy = sdbus::createProxy(
          connection, sdbus::ServiceName{"org.freedesktop.NetworkManager"},
          apPath);
      sdbus::Variant vSsid, vStrength;
      apProxy->callMethod("Get")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments("org.freedesktop.NetworkManager.AccessPoint", "Ssid")
          .storeResultsTo(vSsid);
      apProxy->callMethod("Get")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments("org.freedesktop.NetworkManager.AccessPoint",
                         "Strength")
          .storeResultsTo(vStrength);

      auto ssidVec = vSsid.get<vector<uint8_t>>();
      status.ssid = string(ssidVec.begin(), ssidVec.end());
      status.strength = (int)vStrength.get<uint8_t>();
    }
  } catch (...) {
    status.is_up = false;
  }
  return status;
}

// --- Hardware Info Functions ---

struct CpuTimes {
  unsigned long long user = 0, nice = 0, system = 0, idle = 0;
  unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0;
  unsigned long long total() const {
    return user + nice + system + idle + iowait + irq + softirq + steal;
  }
  unsigned long long active() const { return total() - idle - iowait; }
};

CpuTimes readCpuTimes() {
  ifstream file("/proc/stat");
  string line;
  CpuTimes times;
  if (getline(file, line)) {
    istringstream iss(line);
    string label;
    iss >> label >> times.user >> times.nice >> times.system >> times.idle >>
        times.iowait >> times.irq >> times.softirq >> times.steal;
  }
  return times;
}

string getCpuUsageString() {
  static CpuTimes t1 = readCpuTimes();
  CpuTimes t2 = readCpuTimes();
  unsigned long long activeDiff = t2.active() - t1.active();
  unsigned long long totalDiff = t2.total() - t1.total();
  t1 = t2;
  double usage = (totalDiff > 0) ? (100.0 * activeDiff / totalDiff) : 0.0;
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%4.1f", usage);
  return string(buffer);
}

struct MemInfo {
  unsigned long long total = 0, free = 0, available = 0;
  unsigned long long used() const { return total - available; }
  double usedPercent() const { return total ? 100.0 * used() / total : 0.0; }
};

MemInfo readMemInfo() {
  ifstream file("/proc/meminfo");
  string line, key, unit;
  unsigned long long value;
  MemInfo mem;
  while (getline(file, line)) {
    istringstream iss(line);
    iss >> key >> value >> unit;
    if (key == "MemTotal:")
      mem.total = value;
    else if (key == "MemFree:")
      mem.free = value;
    else if (key == "MemAvailable:")
      mem.available = value;
  }
  return mem;
}

string formatMemory(double kb) {
  double val = kb;
  string unit = "Ki";
  if (val >= 1024 * 1024) {
    val /= (1024 * 1024);
    unit = "Gi";
  } else if (val >= 1024) {
    val /= 1024;
    unit = "Mi";
  }
  stringstream ss;
  ss << fixed << setprecision(1) << val;
  return ss.str();
}

string getMemUsedStr() { return formatMemory(readMemInfo().used()); }
string getMemUnitStr() {
  double kb = readMemInfo().used();
  if (kb >= 1024 * 1024)
    return "Gi";
  if (kb >= 1024)
    return "Mi";
  return "kB";
}
string getMemPercentStr() {
  return to_string((int)round(readMemInfo().usedPercent()));
}

// --- Text/Unicode Handling ---

vector<string> utf8ToChars(const string &str) {
  vector<string> chars;
  for (size_t i = 0; i < str.size();) {
    unsigned char c = str[i];
    size_t len = 1;
    if ((c & 0x80) == 0)
      len = 1;
    else if ((c & 0xE0) == 0xC0)
      len = 2;
    else if ((c & 0xF0) == 0xE0)
      len = 3;
    else if ((c & 0xF8) == 0xF0)
      len = 4;
    chars.push_back(str.substr(i, len));
    i += len;
  }
  return chars;
}

int getCharWidth(const string &utf8char) {
  if (utf8char.length() < 1)
    return 0;
  unsigned char c1 = utf8char[0];
  if (c1 < 0x80)
    return 1;

  // Thai floating/combining marks (zero width)
  if (utf8char.length() >= 3) {
    unsigned char c2 = utf8char[1];
    unsigned char c3 = utf8char[2];
    // Thai combining marks: U+0E31, U+0E34-U+0E3A, U+0E47-U+0E4E
    if (c1 == 0xE0 && c2 == 0xB8) {
      // U+0E31, U+0E34-U+0E3A
      if (c3 == 0xB1 || (c3 >= 0xB4 && c3 <= 0xBA))
        return 0;
    }
    if (c1 == 0xE0 && c2 == 0xB9) {
      // U+0E47-U+0E4E (Thai tone marks)
      if (c3 >= 0x87 && c3 <= 0x8E)
        return 0;
    }
  }

  // CJK (Chinese, Japanese, Korean) - width 2
  if (c1 >= 0xE4 && c1 <= 0xEB)
    return 2;

  return 1;
}

int getStringWidth(const vector<string> &chars) {
  int width = 0;
  for (const auto &ch : chars)
    width += getCharWidth(ch);
  return width;
}

string formatArtist(const string &artist) {
  string res = artist;
  transform(res.begin(), res.end(), res.begin(), ::toupper);
  string suffix = " - TOPIC";
  if (res.size() >= suffix.size() &&
      res.compare(res.size() - suffix.size(), suffix.size(), suffix) == 0)
    res.erase(res.size() - suffix.size());
  return res;
}

// --- Status Components ---

string clock_time() {
  time_t now = time(nullptr);
  tm *timeinfo = localtime(&now);
  char buffer[16];
  strftime(buffer, sizeof(buffer), "%H:%M", timeinfo);
  return "^c" + black + "^ ^b" + darkblue + "^ 󱑆 ^c" + black + "^^b" + blue +
         "^ " + string(buffer) + " ^d^^c" + blue + "^";
}

string kb_layout() {
  string layout =
      trim(exec("xkblayout-state print \"%s\" | tr '[:lower:]' '[:upper:]'"));
  if (layout.empty())
    return "";
  return "^c" + black + "^ ^b" + blue + "^ " + layout + " ^d^^c" + blue + "^";
}

// --- DBus Player Logic ---

string get_playing_player() {
  try {
    auto &connection = getBus();
    auto proxy = sdbus::createProxy(connection,
                                    sdbus::ServiceName{"org.freedesktop.DBus"},
                                    sdbus::ObjectPath{"/org/freedesktop/DBus"});
    vector<string> names;
    proxy->callMethod("ListNames")
        .onInterface("org.freedesktop.DBus")
        .storeResultsTo(names);
    for (const auto &name : names) {
      if (name.find("org.mpris.MediaPlayer2.") != 0)
        continue;
      auto playerProxy =
          sdbus::createProxy(connection, sdbus::ServiceName{name},
                             sdbus::ObjectPath{"/org/mpris/MediaPlayer2"});
      sdbus::Variant vStatus;
      playerProxy->callMethod("Get")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments("org.mpris.MediaPlayer2.Player", "PlaybackStatus")
          .storeResultsTo(vStatus);
      if (vStatus.get<string>() == "Playing")
        return name;
    }
  } catch (...) {
  }
  return "";
}

string player_info(const string &app) {
  try {
    auto &connection = getBus();
    auto proxy =
        sdbus::createProxy(connection, sdbus::ServiceName{app},
                           sdbus::ObjectPath{"/org/mpris/MediaPlayer2"});
    sdbus::Variant vMetadata;
    proxy->callMethod("Get")
        .onInterface("org.freedesktop.DBus.Properties")
        .withArguments("org.mpris.MediaPlayer2.Player", "Metadata")
        .storeResultsTo(vMetadata);
    auto metaMap = vMetadata.get<map<string, sdbus::Variant>>();

    string rawArtist = "Unknown Artist";
    if (metaMap.count("xesam:artist")) {
      try {
        rawArtist = metaMap["xesam:artist"].get<vector<string>>()[0];
      } catch (...) {
        try {
          rawArtist = metaMap["xesam:artist"].get<string>();
        } catch (...) {
        }
      }
    }
    static string prevTitle;
    string TITLE = metaMap.count("xesam:title")
                       ? metaMap["xesam:title"].get<string>()
                       : "Unknown Track";
    if (TITLE != prevTitle) {
      g_scroll_index = 0;
      prevTitle = TITLE;
    }
    string TEXT = "[" + formatArtist(rawArtist) + "]" + "  " + TITLE + " ";
    TEXT = escape_quotes(TEXT);

    double pos = 0, len = 0;
    try {
      sdbus::Variant vPos;
      proxy->callMethod("Get")
          .onInterface("org.freedesktop.DBus.Properties")
          .withArguments("org.mpris.MediaPlayer2.Player", "Position")
          .storeResultsTo(vPos);
      try {
        pos = vPos.get<int64_t>() / 1000000.0;
      } catch (...) {
        pos = vPos.get<uint64_t>() / 1000000.0;
      }
    } catch (...) {
    }

    if (metaMap.count("mpris:length")) {
      try {
        len = metaMap["mpris:length"].get<int64_t>() / 1000000.0;
      } catch (...) {
        len = metaMap["mpris:length"].get<uint64_t>() / 1000000.0;
      }
    }

    double percent = (len > 0.1) ? min((pos / len) * 100.0, 100.0) : 0.0;
    vector<string> chars = utf8ToChars(TEXT);
    if (chars.empty())
      return "";
    if (g_scroll_index >= (int)chars.size())
      g_scroll_index = 0;

    // Count Japanese characters and reduce width accordingly
    int japaneseCount = 0;
    for (const auto &ch : chars) {
      if (getCharWidth(ch) == 2)
        japaneseCount++;
    }
    int SCROLL_WIDTH = 30 - (japaneseCount / 3);

    vector<string> scroll_pool = chars;
    scroll_pool.insert(scroll_pool.end(), chars.begin(), chars.end());

    // Adjust scroll width if text shorter than bar
    int textWidth = getStringWidth(chars);
    double mapped;
    string LAST_BLOCK;
    int displayWidth = SCROLL_WIDTH;

    if (textWidth <= SCROLL_WIDTH) {
      displayWidth = textWidth;            // use actual visual width
      mapped = min(percent / 0.95, 100.0); // map 0–85% to 0–100%
    } else {
      mapped = min(percent / 0.855, 100.0); // map 0–90% to 0–100%
      if (percent >= 95) {
        LAST_BLOCK = "^c" + green2 + "^^b" + green2 + "^ ";
      } else {
        LAST_BLOCK = " ";
      }
    }

    int prog_w = static_cast<int>((mapped / 100.0) * displayWidth);
    string BEFORE, AFTER;
    int cur_w = 0, j = 0;
    static int japaneseInAfter = 0;  // Track Japanese chars in AFTER block
    static int reductionPending = 0; // How many chars to reduce from BEFORE

    // Fill BEFORE block (with reduction if pending)
    int beforeReduction = reductionPending;
    while (cur_w < prog_w && j < (int)scroll_pool.size()) {
      int cw =
          getCharWidth(scroll_pool[(g_scroll_index + j) % scroll_pool.size()]);
      if (cur_w + cw <= prog_w) {
        if (beforeReduction > 0) {
          // Skip this character to reduce BEFORE block
          beforeReduction--;
          j++;
        } else {
          BEFORE += scroll_pool[(g_scroll_index + j) % scroll_pool.size()];
          cur_w += cw;
          j++;
        }
      } else
        break;
    }
    reductionPending = 0; // Reset reduction

    // Fill AFTER block and count Japanese characters
    japaneseInAfter = 0;
    while (cur_w < displayWidth && j < (int)scroll_pool.size()) {
      int cw =
          getCharWidth(scroll_pool[(g_scroll_index + j) % scroll_pool.size()]);
      if (cur_w + cw <= displayWidth) {
        AFTER += scroll_pool[(g_scroll_index + j) % scroll_pool.size()];
        if (cw == 2)
          japaneseInAfter++; // Count Japanese characters
        cur_w += cw;
        j++;
      } else
        break;
    }

    // If we have 3+ Japanese characters in AFTER, reduce BEFORE by 2 chars next
    // time
    if (japaneseInAfter >= 3) {
      reductionPending = 2;
    }

    // Only scroll if text is longer than bar
    if (textWidth > SCROLL_WIDTH) {
      g_scroll_index = (g_scroll_index + 1) % chars.size();
    }
    return "^c" + black + "^ ^b" + green2 + "^ 󰎆 ^c" + black + "^" + BEFORE +
           "^b" + green3 + "^" + AFTER + LAST_BLOCK + "^d^^c" + blue + "^";
  } catch (...) {
    return "^c" + black + "^ ^b" + green2 + "^ 󰎆 Music ";
  }
}

// --- Main Logic ---

bool fan_st = 0;

bool removeTmp(const std::string& filename) {
    std::string path = "/tmp/" + filename;
    if (std::remove(path.c_str()) != 0) {
        return false;
    }
    return true;
}

string sav_perf_icon = "";
string sav_fan_icon = "";


string handle() {
  string tlp = read_file("/run/tlp/last_pwr");
  
  if ( tlp == "0 0" && fan_st == 0){
    //printf("%s",tlp);
    setFan("0");
    fan_st=1;
  } else if (tlp != "0 0" && fan_st==1){
    //printf("%s",tlp);
    setFan("2");
    fan_st=0;
  }
  
  auto f_wifi = pool.enqueue(get_wifi_info);
  auto f_mem_used = pool.enqueue(getMemUsedStr);
  auto f_mem_unit = pool.enqueue(getMemUnitStr);
  auto f_mem_perc = pool.enqueue(getMemPercentStr);
  auto f_cpu = pool.enqueue(getCpuUsageString);
  auto f_player = pool.enqueue(get_playing_player);
  auto f_kb = pool.enqueue(kb_layout);

  WifiStatus wifi = f_wifi.get();
  string free_output = f_mem_used.get();
  string mem_unit = f_mem_unit.get();
  int mem_percent = stoi(f_mem_perc.get());
  string cpu_usage = f_cpu.get();
  string player = f_player.get();
  string kb = f_kb.get();

  string icon = (mem_percent < 40)   ? "󰾆"
                : (mem_percent < 80) ? "󰾅"
                                     : "󰓅";
  string t_color = (mem_percent < 80) ? black : red;

  string wicon;
  if (!wifi.is_up) {
    wicon = "󰤭";
  } else {
    if (wifi.strength > 75)
      wicon = "󰤨";
    else if (wifi.strength > 50)
      wicon = "󰤥";
    else if (wifi.strength > 25)
      wicon = "󰤢";
    else
      wicon = "󰤟";
  }
  
  string cap_s = read_file("/sys/class/power_supply/BAT0/capacity");
  string chg_s = read_file("/sys/class/power_supply/AC0/online");
  int capacity = cap_s.empty() ? 0 : stoi(cap_s);
  int charging = chg_s.empty() ? 0 : stoi(chg_s);
  string bicon = (charging == 1)    ? "󰂄"
                 : (capacity >= 90) ? "󰁹"
                 : (capacity >= 50) ? "󰁿"
                 : (capacity >= 20) ? "󰁼"
                                    : "󰁺";

  if ( read_file(pwmPath) == "2" ){
    sav_fan_icon="";
  } else sav_fan_icon="";
  if ( read_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")!="performance" ){
    sav_perf_icon="";
  } else sav_perf_icon="";
  
  string result;

  if (!player.empty()) {
    result = player_info(player) + "^c" + t_color + "^ ^b" + blue + "^ " +
             icon + " ^d^";
    result += "^c" + black + "^ ^b" + blue + "^ " + wicon + " ^d^" +
              clock_time() + kb + "^c" + blue + "^ " + bicon;
    if (DimMode == 0) {
      setAutoDimTimeout(dpy, 0);
      DimMode = 0;
    }
  } else {
    g_scroll_index = 0;
    result = "^c" + grey + "^" + sav_fan_icon + " " + sav_perf_icon + "  ^c" + blue + "^" + bicon + " " + to_string(capacity) + "%^c" +
             black + "^ ^b" + green + "^ ^c" + white + "^ ^b" + grey + "^ " +
             cpu_usage + "% ^d^";
    result += "^c" + t_color + "^ ^b" + green + "^ " + icon + "^c" + white +
              "^ ^b" + grey + "^ " + free_output + mem_unit + " ^d^";
    result += "^c" + black + "^ ^b" + blue + "^ " + wicon + "^c" + white +
              "^ ^b" + grey + "^ " + (wifi.is_up ? wifi.ssid : "Disconnected") +
              " ^d^" + clock_time() + kb;
    if (DimMode == 1) {
      setAutoDimTimeout(dpy, 600);
      DimMode = 1;
    }
  }
  return result;
}

int main() {
  dpy = getDisplay();
  while (true) {
    if (!read_fullscreen()) {
      string bar_output = "   " + handle() + "^b" + black + "^ ";
      string cmd = "xsetroot -name \"" + bar_output + "\"";
      system(cmd.c_str());
      usleep(700000);
    } else {
      usleep(5000000);
    }
  }
  return 0;
}