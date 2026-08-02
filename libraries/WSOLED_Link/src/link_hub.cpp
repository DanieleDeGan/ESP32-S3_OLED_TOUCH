/**
 * link_hub.cpp
 *
 * Ruolo hub/master: mentre la modalita' pairing e' attiva, accetta HELLO in
 * broadcast da MAC sconosciuti e li aggiunge al registro; il WELCOME di
 * risposta NON viene inviato da dentro il callback di ricezione (che gira
 * nel task del driver WiFi e va tenuto breve, stessa regola gia' in
 * CLAUDE.md per i callback LVGL) ma accodato e inviato da Link_Hub_Poll(),
 * chiamato dal loop() dello sketch.
 *
 * Il registro peer e' scritto dal callback di ricezione (task del driver
 * WiFi) e letto da loop()/task LVGL quando l'app aggiorna la UI: accesso
 * concorrente reale, protetto da una portMUX_TYPE (a differenza di
 * Core_I2CBusInit(), chiamato solo in sequenza da setup() e quindi senza
 * bisogno di lock).
 */

#include "link_peer.h"

#include <Arduino.h>
#include <string.h>
#include "freertos/FreeRTOS.h"

#define WSOLED_LINK_MAX_PEERS ESP_NOW_MAX_TOTAL_PEER_NUM

static LinkPeer *s_peers[WSOLED_LINK_MAX_PEERS] = {nullptr};
static int s_peer_count = 0;
static volatile bool s_pairing_mode = false;
static portMUX_TYPE s_registry_mux = portMUX_INITIALIZER_UNLOCKED;

static void hub_on_new_peer(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg)
{
    bool broadcast = memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) == 0;
    if (!broadcast || !s_pairing_mode) {
        return;   // fuori dalla finestra di pairing: ignora mittenti sconosciuti
    }

    link_message_t msg;
    if (!link_parse_message(data, len, &msg) || msg.msg_type != LINK_MSG_HELLO) {
        return;
    }

    portENTER_CRITICAL(&s_registry_mux);
    bool full = (s_peer_count >= WSOLED_LINK_MAX_PEERS);
    portEXIT_CRITICAL(&s_registry_mux);
    if (full) {
        return;
    }

    LinkPeer *peer = new LinkPeer(info->src_addr, g_link_channel);
    if (!peer->addPeer()) {
        delete peer;
        return;
    }
    peer->nodeType = (link_node_type_t)msg.node_type;
    strncpy(peer->name, msg.name, LINK_NAME_LEN - 1);
    peer->name[LINK_NAME_LEN - 1] = '\0';
    peer->lastSeenMs = millis();
    peer->welcomePending = true;

    portENTER_CRITICAL(&s_registry_mux);
    s_peers[s_peer_count++] = peer;
    portEXIT_CRITICAL(&s_registry_mux);

    link_notify_app(info->src_addr, &msg);
}

void link_hub_start(void)
{
    ESP_NOW.onNewPeer(hub_on_new_peer, nullptr);
}

void Link_Hub_Poll(void)
{
    portENTER_CRITICAL(&s_registry_mux);
    for (int i = 0; i < s_peer_count; i++) {
        LinkPeer *peer = s_peers[i];
        if (peer != nullptr && peer->welcomePending) {
            portEXIT_CRITICAL(&s_registry_mux);

            link_message_t welcome = {};
            welcome.protocol_version = LINK_PROTOCOL_VERSION;
            welcome.msg_type = LINK_MSG_WELCOME;
            welcome.node_type = LINK_NODE_HUB;
            strncpy(welcome.name, g_link_self_name, LINK_NAME_LEN - 1);
            bool sent = peer->sendReliable(welcome);

            portENTER_CRITICAL(&s_registry_mux);
            // Pulisce il flag SOLO se l'invio e' andato a buon fine: un fallimento
            // (es. transitorio, appena dopo l'aggiunta del peer) viene ritentato
            // al prossimo giro di loop() invece di perdere per sempre la finestra
            // di pairing di quel nodo.
            if (sent) {
                peer->welcomePending = false;
            }
        }
    }
    portEXIT_CRITICAL(&s_registry_mux);
}

void Link_Hub_SetPairingMode(bool enable)
{
    s_pairing_mode = enable;
}

int Link_Hub_GetPeerCount(void)
{
    portENTER_CRITICAL(&s_registry_mux);
    int n = s_peer_count;
    portEXIT_CRITICAL(&s_registry_mux);
    return n;
}

bool Link_Hub_GetPeerInfo(int index, uint8_t mac_out[6], link_node_type_t *type_out,
                          char name_out[LINK_NAME_LEN], uint32_t *last_seen_ms_out,
                          link_message_t *last_data_out)
{
    portENTER_CRITICAL(&s_registry_mux);
    if (index < 0 || index >= s_peer_count || s_peers[index] == nullptr) {
        portEXIT_CRITICAL(&s_registry_mux);
        return false;
    }
    LinkPeer *peer = s_peers[index];
    if (mac_out) memcpy(mac_out, peer->addr(), 6);
    if (type_out) *type_out = peer->nodeType;
    if (name_out) {
        strncpy(name_out, peer->name, LINK_NAME_LEN - 1);
        name_out[LINK_NAME_LEN - 1] = '\0';
    }
    if (last_seen_ms_out) *last_seen_ms_out = peer->lastSeenMs;
    if (last_data_out) {
        if (peer->hasData) {
            *last_data_out = peer->lastData;
        } else {
            memset(last_data_out, 0, sizeof(link_message_t));
        }
    }
    portEXIT_CRITICAL(&s_registry_mux);
    return true;
}

bool Link_Hub_SendCommand(const uint8_t mac[6], link_message_t *msg)
{
    if (mac == nullptr || msg == nullptr) {
        return false;
    }

    portENTER_CRITICAL(&s_registry_mux);
    LinkPeer *target = nullptr;
    for (int i = 0; i < s_peer_count; i++) {
        if (s_peers[i] != nullptr && memcmp(s_peers[i]->addr(), mac, 6) == 0) {
            target = s_peers[i];
            break;
        }
    }
    portEXIT_CRITICAL(&s_registry_mux);
    if (target == nullptr) {
        return false;
    }

    msg->protocol_version = LINK_PROTOCOL_VERSION;
    msg->msg_type = LINK_MSG_COMMAND;
    msg->node_type = LINK_NODE_HUB;
    return target->sendReliable(*msg);
}
