/**
 * link_peer.h
 *
 * Dettagli interni condivisi tra link_peer.cpp/link_node.cpp/link_hub.cpp.
 * NON e' l'header pubblico della libreria (quello e' WSOLED_Link.h): questi
 * simboli non sono pensati per essere usati dallo sketch applicativo.
 */

#ifndef WSOLED_LINK_PEER_H
#define WSOLED_LINK_PEER_H

#include "WSOLED_Link.h"
#include "ESP32_NOW.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/** Copia (memcpy, mai cast diretto) e valida un payload ricevuto. Ritorna
 *  false se la lunghezza non combacia o protocol_version non corrisponde. */
bool link_parse_message(const uint8_t *data, int len, link_message_t *out);

/** Notifica la callback applicativa registrata con Link_OnMessage(), se presente. */
void link_notify_app(const uint8_t mac[6], const link_message_t *msg);

/** Identita' di questo dispositivo, impostata da Link_Init(), letta da
 *  entrambi i ruoli (usata per timbrare name/node_type sui messaggi in uscita). */
extern link_node_type_t g_link_self_type;
extern char g_link_self_name[LINK_NAME_LEN];

/** Canale con cui registrare i peer, impostato da Link_InitEx() (0 =
 *  "canale corrente", vedi WSOLED_LINK_CHANNEL_CURRENT). Usarlo sempre al
 *  posto della costante WSOLED_LINK_CHANNEL quando si crea un LinkPeer. */
extern uint8_t g_link_channel;

/** Setup specifico di ruolo, chiamato una sola volta da Link_Init() in base
 *  a g_link_self_type (implementati in link_node.cpp / link_hub.cpp). */
void link_node_start(void);
void link_hub_start(void);

/**
 * Un peer ESP-NOW (nodo visto dall'hub, oppure l'hub visto dal nodo).
 * onReceive() e' generico rispetto al ruolo: valida il payload, aggiorna
 * lastSeenMs/lastData se e' un DATA, poi notifica sempre l'app — sta
 * all'applicazione (o al chiamante del ruolo) decidere cosa fare in base a
 * msg_type (es. un nodo reagisce a un COMMAND, l'hub aggrega i DATA).
 */
class LinkPeer : public ESP_NOW_Peer {
public:
    LinkPeer(const uint8_t *mac_addr, uint8_t channel);
    ~LinkPeer() override;

    bool addPeer();

    /** Accoda l'invio (fire-and-forget): true se accettato localmente, NON
     *  garantisce la consegna over-the-air (l'ACK arriva in modo asincrono
     *  via onSent()). Vedi sendReliable() per un invio con conferma+ritentativi. */
    bool sendMessage(const link_message_t &msg);

    /**
     * Come sendMessage(), ma attende la conferma di consegna (onSent) e
     * ritenta fino a max_attempts volte se l'ACK non arriva entro
     * ack_timeout_ms — la perdita occasionale di frame unicast e' normale su
     * ESP-NOW (osservata anche tra due nodi identici), non solo tra chip di
     * generazioni diverse. Bloccante per al massimo
     * ~max_attempts*(ack_timeout_ms + 50ms): accettabile per messaggi non a
     * altissima frequenza (HELLO/WELCOME/DATA/COMMAND di questa libreria).
     */
    bool sendReliable(const link_message_t &msg, int max_attempts = 3, uint32_t ack_timeout_ms = 1000);   // DIAGNOSTICA TEMPORANEA: timeout allargato per misurare la latenza reale

    void onReceive(const uint8_t *data, size_t len, bool broadcast) override;
    void onSent(bool success) override;

    link_node_type_t nodeType = LINK_NODE_UNKNOWN;
    char name[LINK_NAME_LEN] = {0};
    uint32_t lastSeenMs = 0;
    link_message_t lastData = {};
    bool hasData = false;
    bool welcomePending = false;   // lato hub: WELCOME accodato, non ancora inviato (vedi Link_Hub_Poll)

private:
    // Segnala da onSent() (task del driver WiFi, spesso Core 0) a sendReliable()
    // (chiamante su loop(), spesso Core 1): un semaforo FreeRTOS da' la
    // sincronizzazione cross-core che un semplice "volatile bool" NON garantisce
    // (volatile impedisce solo il riordino del compilatore, non la visibilita'
    // tra core su un chip dual-core — con un bool nudo il ritentativo rischiava
    // di non vedere mai la conferma in tempo ed esaurire sempre tutti i tentativi
    // anche a invio riuscito).
    SemaphoreHandle_t ackSem = nullptr;
    bool lastAckSuccess = false;   // letto solo subito dopo un xSemaphoreTake riuscito
};

#endif // WSOLED_LINK_PEER_H
