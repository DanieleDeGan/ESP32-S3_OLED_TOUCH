/**
 * link_node.cpp
 *
 * Ruolo nodo/periferica: annuncia HELLO in broadcast finche' non associato,
 * impara il MAC dell'hub dal WELCOME (via il proprio onNewPeer, simmetrico a
 * quello dell'hub — vedi la nota in WSOLED_Link.h sulla scoperta
 * bidirezionale), poi invia DATA in unicast.
 */

#include "link_peer.h"

#include <Arduino.h>
#include <string.h>

#define LINK_HELLO_INTERVAL_MS 2000

static LinkPeer *s_broadcast_peer = nullptr;
static LinkPeer *s_hub_peer = nullptr;
static bool s_paired = false;
static uint32_t s_last_hello_ms = 0;
static uint32_t s_seq = 0;

static void node_on_new_peer(const esp_now_recv_info_t *info, const uint8_t *data, int len, void *arg)
{
    if (s_paired) {
        return;   // gia' associato: non ci si "ri-accoppia" con un secondo hub
    }

    link_message_t msg;
    if (!link_parse_message(data, len, &msg)) {
        return;
    }
    if (msg.msg_type != LINK_MSG_WELCOME || msg.node_type != LINK_NODE_HUB) {
        return;
    }

    LinkPeer *hub = new LinkPeer(info->src_addr, g_link_channel);
    if (!hub->addPeer()) {
        delete hub;
        return;
    }
    s_hub_peer = hub;
    s_paired = true;
    link_notify_app(info->src_addr, &msg);
}

void link_node_start(void)
{
    s_broadcast_peer = new LinkPeer(ESP_NOW.BROADCAST_ADDR, g_link_channel);
    s_broadcast_peer->addPeer();
    ESP_NOW.onNewPeer(node_on_new_peer, nullptr);
}

void Link_Node_Poll(void)
{
    if (s_paired || s_broadcast_peer == nullptr) {
        return;
    }
    uint32_t now = millis();
    if (now - s_last_hello_ms < LINK_HELLO_INTERVAL_MS) {
        return;
    }
    s_last_hello_ms = now;

    link_message_t hello = {};
    hello.protocol_version = LINK_PROTOCOL_VERSION;
    hello.msg_type = LINK_MSG_HELLO;
    hello.node_type = g_link_self_type;
    strncpy(hello.name, g_link_self_name, LINK_NAME_LEN - 1);
    hello.seq = s_seq++;
    s_broadcast_peer->sendMessage(hello);
}

bool Link_Node_IsPaired(void)
{
    return s_paired;
}

bool Link_Node_SendData(link_message_t *msg)
{
    if (!s_paired || s_hub_peer == nullptr || msg == nullptr) {
        return false;
    }
    msg->protocol_version = LINK_PROTOCOL_VERSION;
    msg->msg_type = LINK_MSG_DATA;
    msg->node_type = g_link_self_type;
    strncpy(msg->name, g_link_self_name, LINK_NAME_LEN - 1);
    msg->name[LINK_NAME_LEN - 1] = '\0';
    msg->seq = s_seq++;
    return s_hub_peer->sendReliable(*msg);
}
