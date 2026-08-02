#include "hub_link.h"

#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static uint8_t          s_channel = 0;
static bool             s_ready   = false;
static hub_command_cb_t s_cmdCb   = nullptr;
static QueueHandle_t    s_cmdQueue = nullptr;
static uint8_t          s_hubMac[6] = {0};
static char             s_hubMacStr[18] = "-";

// ---------------------------------------------------------------------
//  Callback ESP-NOW: gira nel task del driver WiFi, NON in loop().
//  Regola della casa (la stessa dei callback LVGL sull'hub): qui dentro
//  niente lavoro lento e nessun invio radio — solo accodare.
// ---------------------------------------------------------------------
static void on_link_message(const uint8_t mac[6], const link_message_t* msg) {
  if (msg->msg_type == LINK_MSG_WELCOME) {
    memcpy(s_hubMac, mac, 6);
    return;
  }
  if (msg->msg_type == LINK_MSG_COMMAND && s_cmdQueue) {
    // Una coda FreeRTOS, non un flag volatile: il passaggio e' tra core
    // diversi e su un flag nudo la visibilita' non e' garantita (e' lo
    // stesso motivo per cui LinkPeer usa un semaforo per gli ACK).
    int cmd = (int)msg->value[0];
    xQueueSend(s_cmdQueue, &cmd, 0);
  }
}

bool hub_begin(const char* node_name) {
  s_cmdQueue = xQueueCreate(4, sizeof(int));

  // Connessi a un AP: il canale non e' nostro, lo subiamo (e lo comunichiamo
  // in chiaro, perche' l'hub va messo li'). Senza AP: canale fisso della
  // libreria, l'hub standard va bene com'e'.
  const bool onAp = (WiFi.status() == WL_CONNECTED);
  if (!Link_InitEx(LINK_NODE_CAMERA, node_name,
                   onAp ? WSOLED_LINK_CHANNEL_CURRENT : WSOLED_LINK_CHANNEL)) {
    Serial.println("[LINK] init ESP-NOW fallita");
    return false;
  }
  Link_OnMessage(on_link_message);
  s_ready = true;
  s_channel = onAp ? WiFi.channel() : WSOLED_LINK_CHANNEL;
  Serial.printf("[LINK] nodo camera \"%s\" sul canale %u (%s): l'hub deve stare sullo stesso\n",
                node_name, (unsigned)s_channel, onAp ? "canale dell'AP" : "canale fisso, niente WiFi");
  return true;
}

uint8_t hub_channel() { return s_channel; }

void hub_loop() {
  if (!s_ready) return;
  Link_Node_Poll();

  int cmd;
  while (s_cmdQueue && xQueueReceive(s_cmdQueue, &cmd, 0) == pdTRUE) {
    if (s_cmdCb) s_cmdCb(cmd);
  }

  // Il MAC dell'hub arriva dalla callback radio: la stringa la componiamo
  // qui, nel contesto di loop().
  if (s_hubMacStr[0] == '-' && (s_hubMac[0] || s_hubMac[1] || s_hubMac[2])) {
    snprintf(s_hubMacStr, sizeof(s_hubMacStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             s_hubMac[0], s_hubMac[1], s_hubMac[2], s_hubMac[3], s_hubMac[4], s_hubMac[5]);
  }
}

bool hub_ready()  { return s_ready; }
bool hub_paired() { return s_ready && Link_Node_IsPaired(); }

void hub_on_command(hub_command_cb_t cb) { s_cmdCb = cb; }

bool hub_notify_motion(uint32_t event_n, long photo_index, bool photo_saved) {
  if (!hub_paired()) return false;

  link_message_t msg = {};
  msg.battery_mv = 0;              // alimentazione fissa
  msg.value[0] = (float)event_n;
  msg.value[1] = (float)photo_index;
  msg.value[2] = photo_saved ? 1.0f : 0.0f;
  return Link_Node_SendData(&msg);   // timbra da solo tipo/nome/seq
}

const char* hub_mac_str() { return s_hubMacStr; }
