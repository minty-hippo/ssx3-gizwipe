// SSX 3 GizWipe - local wireless multiplayer (see net.h). DSWifi's multiplayer transfers: the host sends a CMD frame
// every video frame with the whole session state (NetHost); each client's reply (NetClient) rides back in the same
// contention-free period. A packet that is lost is simply replaced by the next frame's.
#include <string.h>
#include <stdio.h>
#include <dswifi9.h>
#include "net.h"

#define GAME_ID 0x53535833u                      // 'SSX3'
#define MAGIC 0x5a

NetHost net_s;
NetCtl net_ctl;
NetRider net_rider;
u8 net_pending;
u32 net_rx_frames;
static bool inited, active, host, joining;
static int me;
static char my_name[10];
static NetClient rx_cl[NET_MAX];                 // host: the last reply of each client
static int age[NET_MAX];                         // frames since each player was heard
static u8 seen;                                  // host: players ever heard (to raise "wants to join")
static u8 kick_t[NET_MAX];                       // host: rejected, kicked after this many frames (they see it first)
static volatile bool rx_new;
static NetHost rx_host;                          // client: the last host frame
static Wifi_AccessPoint aps[8]; static int naps;
static u16 my_mac;

static void from_host(Wifi_MPPacketType type, int base, int len)
{
    if (type != WIFI_MPTYPE_CMD || len < (int)sizeof(NetHost)) return;
    NetHost h; Wifi_RxRawReadPacket(base, sizeof(h), &h);
    if (h.magic != MAGIC) return;
    rx_host = h; rx_new = true;
}

static void from_client(Wifi_MPPacketType type, int aid, int base, int len)
{
    if (type != WIFI_MPTYPE_REPLY || len < (int)sizeof(NetClient) || aid < 1 || aid >= NET_MAX) return;
    NetClient c; Wifi_RxRawReadPacket(base, sizeof(c), &c);
    if (c.magic != MAGIC) return;
    rx_cl[aid] = c; age[aid] = 0;
}

bool net_init(void)
{
    if (inited) return true;
    if (!Wifi_InitDefault(INIT_ONLY | WIFI_LOCAL_ONLY)) return false;
    net_my_name();
    u8 mac[6]; Wifi_GetData(WIFIGETDATA_MACADDRESS, 6, mac); my_mac = mac[4] | (mac[5] << 8);
    inited = true;
    return true;
}

static void reset_state(void)
{
    memset(&net_s, 0, sizeof(net_s)); memset(&net_ctl, 0, sizeof(net_ctl)); memset(&net_rider, 0, sizeof(net_rider));
    memset(rx_cl, 0, sizeof(rx_cl)); memset(age, 0, sizeof(age)); memset(kick_t, 0, sizeof(kick_t));
    net_s.magic = MAGIC; net_s.paused_by = 0xff; net_s.phase = NP_LOBBY;
    for (int i = 0; i < NET_MAX; i++) net_s.ctl[i].rider = 0xff;
    strcpy(net_ctl.name, my_name); net_ctl.rider = 0xff;
    seen = 1; net_pending = 0; net_rx_frames = 0; rx_new = false;
}

void net_host(void)
{
    if (!net_init()) return;
    reset_state();
    Wifi_MultiplayerHostMode(NET_MAX - 1, sizeof(NetHost), sizeof(NetClient));
    Wifi_MultiplayerFromClientSetPacketHandler(from_client);
    while (!Wifi_LibraryModeReady()) swiWaitForVBlank();
    Wifi_SetChannel(6);
    Wifi_MultiplayerAllowNewClients(true);
    Wifi_BeaconStart("SSX3 GizWipe", GAME_ID);
    host = true; active = true; me = 0; net_s.mask = 1;
}

void net_scan(void)
{
    if (!net_init()) return;
    reset_state();
    Wifi_MultiplayerClientMode(sizeof(NetClient));
    Wifi_MultiplayerFromHostSetPacketHandler(from_host);
    while (!Wifi_LibraryModeReady()) swiWaitForVBlank();
    Wifi_ScanMode();
    host = false; active = false; joining = false; naps = 0;
}

int net_games(NetGame *out, int max)
{
    int n = Wifi_GetNumAP(), k = 0; naps = 0;
    for (int i = 0; i < n && k < max && naps < 8; i++) {
        Wifi_AccessPoint ap;
        if (Wifi_GetAPData(i, &ap) != WIFI_RETURN_OK) continue;
        if (ap.nintendo.game_id != GAME_ID) continue;
        aps[naps] = ap;
        NetGame *g = &out[k++];
        int l = ap.nintendo.name_len; if (l > 23) l = 23;   // (in characters)
        for (int j = 0; j < l; j++) { u16 c = ap.nintendo.name[j]; g->name[j] = c >= 32 && c < 127 ? c : '?'; }
        g->name[l] = 0;
        g->players = ap.nintendo.players_current; g->index = naps++;
    }
    return k;
}

bool net_join(int index)
{
    if (index < 0 || index >= naps) return false;
    Wifi_ConnectOpenAP(&aps[index]);
    joining = true; net_rx_frames = 0;
    return true;
}

int net_join_status(void)
{
    if (!joining) return -1;
    int s = Wifi_AssocStatus();
    if (s == ASSOCSTATUS_CANNOTCONNECT) return -1;
    if (s != ASSOCSTATUS_ASSOCIATED) return 0;
    if (!active) { active = true; me = 0; }
    return 1;
}

void net_leave(void)
{
    if (!inited) return;
    if (!host && joining) Wifi_DisconnectAP();
    Wifi_IdleMode();
    active = host = joining = false; net_s.phase = NP_OFF;
}

bool net_active(void) { return active; }
bool net_is_host(void) { return host; }
int net_me(void) { return me; }
const char *net_my_name(void)
{
    if (!my_name[0]) {                           // the DS's owner name (firmware user settings): the Gizmondo's Device Name
        int n = PersonalData->nameLen; if (n > 9) n = 9;
        for (int i = 0; i < n; i++) { u16 c = PersonalData->name[i]; my_name[i] = c >= 32 && c < 127 ? c : '?'; }
        my_name[n] = 0;
        if (!my_name[0]) strcpy(my_name, "Player");
    }
    return my_name;
}
bool net_connected(int pid) { return pid == 0 ? true : age[pid] < 120 && rx_cl[pid].magic == MAGIC; }
int net_age(int pid) { return host ? age[pid] : (int)net_rx_frames; }

void net_accept(int pid, bool yes)
{
    if (!host || pid < 1 || pid >= NET_MAX) return;
    net_pending &= ~(1 << pid);
    if (yes) net_s.mask |= 1 << pid;
    else kick_t[pid] = 30;                       // half a second for them to read "rejected"
}

void net_update(void)
{
    if (!active) return;
    if (host) {
        u16 assoc = Wifi_MultiplayerGetClientMask();
        Wifi_ConnectedClient cl[NET_MAX];
        int ncl = Wifi_MultiplayerGetClients(NET_MAX, cl);
        memset(net_s.mac, 0, sizeof(net_s.mac)); net_s.mac[0] = my_mac;
        for (int i = 0; i < ncl; i++)
            if (cl[i].association_id > 0 && cl[i].association_id < NET_MAX) net_s.mac[cl[i].association_id] = cl[i].macaddr[2];
        for (int p = 1; p < NET_MAX; p++) {
            age[p]++;
            bool here = (assoc >> p) & 1;
            if (here && rx_cl[p].magic == MAGIC && !((seen >> p) & 1)) { seen |= 1 << p; net_pending |= 1 << p; }
            if (!here || age[p] > 180) {         // gone: free the slot
                if ((seen >> p) & 1) { seen &= ~(1 << p); net_pending &= ~(1 << p); net_s.mask &= ~(1 << p); }
                rx_cl[p].magic = 0; memset(&net_s.ctl[p], 0, sizeof(NetCtl)); net_s.ctl[p].rider = 0xff;
                continue;
            }
            if (kick_t[p] && !--kick_t[p]) { Wifi_MultiplayerKickClientByAID(p); seen &= ~(1 << p); }
            if ((net_s.mask >> p) & 1) { net_s.ctl[p] = rx_cl[p].ctl; net_s.r[p] = rx_cl[p].r; }
            else strncpy(net_s.ctl[p].name, rx_cl[p].ctl.name, 10);   // the name shows in the accept prompt
        }
        // stop taking players once the event is chosen (the Gizmondo closes the session at Starting Game)
        Wifi_MultiplayerAllowNewClients(net_s.phase == NP_LOBBY);
        net_s.ctl[0] = net_ctl; net_s.r[0] = net_rider;
        net_s.seq++;
        for (int p = 1; p < NET_MAX; p++) if (kick_t[p]) net_s.ctl[p].ready = 0xee;   // "[...]"
        Wifi_MultiplayerHostCmdTxFrame(&net_s, sizeof(net_s));
    } else {
        net_rx_frames++;
        if (rx_new) {
            rx_new = false; net_rx_frames = 0;
            net_s = rx_host;
        }
        for (int p = 1; p < NET_MAX; p++) if (net_s.mac[p] == my_mac) me = p;   // my slot (association ID)
        NetClient c = { MAGIC, (u8)me, net_ctl, net_rider };
        Wifi_MultiplayerClientReplyTxFrame(&c, sizeof(c));
    }
}
