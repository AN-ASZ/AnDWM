#include <iostream>
#include <unistd.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <sys/socket.h>
#include <sched.h>
#include <fstream>
#include <vector>
#include <unordered_map>

using namespace std;

using cpu_list = vector<int>;

/* ================= RULES ================= */

unordered_map<string, cpu_list> rules = {
    { "pipewire",       {0,1} },
    { "wireplumber",    {0,1} },
    { "pipewire-pulse", {0,1} },
    { "bat",            {1} },
    { "bar",            {1} },
    { "wired",          {1} }
};

cpu_list default_cpus = []() {
    cpu_list cpus;
    int count = sysconf(_SC_NPROCESSORS_ONLN);
    for (int i = 1; i < count; ++i)
        cpus.push_back(i);
    return cpus;
}();
/* ========================================= */

string read_comm(pid_t pid) {
    ifstream f("/proc/" + to_string(pid) + "/comm");
    string s;
    if (f.good())
        getline(f, s);
    return s;
}

void set_affinity(pid_t pid, const cpu_list& cpus) {
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c : cpus)
        CPU_SET(c, &set);
    sched_setaffinity(pid, sizeof(set), &set);
}

int main() {
    int sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);

    sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = CN_IDX_PROC;
    addr.nl_pid = getpid();
    bind(sock, (sockaddr*)&addr, sizeof(addr));

    /* subscribe */
    char subbuf[NLMSG_SPACE(sizeof(cn_msg) + sizeof(proc_cn_mcast_op))];
    nlmsghdr* nl = (nlmsghdr*)subbuf;
    cn_msg* cn = (cn_msg*)NLMSG_DATA(nl);
    proc_cn_mcast_op* op = (proc_cn_mcast_op*)cn->data;

    nl->nlmsg_len = NLMSG_LENGTH(sizeof(cn_msg) + sizeof(proc_cn_mcast_op));
    nl->nlmsg_type = NLMSG_DONE;
    nl->nlmsg_pid = getpid();

    cn->id.idx = CN_IDX_PROC;
    cn->id.val = CN_VAL_PROC;
    cn->len = sizeof(proc_cn_mcast_op);

    *op = PROC_CN_MCAST_LISTEN;
    send(sock, nl, nl->nlmsg_len, 0);

    //cout << "Affinity daemon running\n";

    char buf[4096];

    while (true) {
        int len = recv(sock, buf, sizeof(buf), 0);
        if (len <= 0) continue;

        nlmsghdr* rnl = (nlmsghdr*)buf;
        cn_msg* rcn = (cn_msg*)NLMSG_DATA(rnl);
        proc_event* ev = (proc_event*)rcn->data;

        if (ev->what != PROC_EVENT_EXEC)
            continue;

        pid_t pid = ev->event_data.exec.process_pid;
        string name = read_comm(pid);

        auto it = rules.find(name);
        if (it != rules.end()) {
            set_affinity(pid, it->second);
            //cout << "[rule] " << name << " → pinned\n";
        } else {
            set_affinity(pid, default_cpus);
        }
    }
}
