// SPDX-License-Identifier: MIT
// IPv4 router, not a Wi-Fi L2 bridge. All USB class calls run on TinyUSB's task.
#include "ouse_usb_private.h"
#include "ouse_devices.h"
#include "device/usbd_pvt.h"
#include "esp_netif.h"
#include "esp_netif_defaults.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/queue.h"
#include "lwip/ip4_addr.h"
#include <string.h>

#define FRAME_MAX 1514
#define PACKETS 4
typedef struct { bool used; uint16_t len; uint8_t data[FRAME_MAX]; } packet_t;
static packet_t *rx_pool,*tx_pool;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static esp_netif_t *usb_netif;
static QueueHandle_t tx_queue;
static bool accepting,pump_pending,netif_up,napt_on;
static uint32_t rx_count,tx_count,dropped;
static bool uplink,conflict;
static bool cleanup_pending;
static esp_err_t last_error;
static int64_t next_sample;
static packet_t *pending_tx; // exclusively owned by TinyUSB task
static uint32_t last_uplink;
static esp_netif_ip_info_t local_ip;
static packet_t *take(packet_t *pool)
{
    packet_t *out=NULL;portENTER_CRITICAL(&lock);
    for(unsigned i=0;i<PACKETS;++i)if(!pool[i].used){out=&pool[i];out->used=true;break;}
    portEXIT_CRITICAL(&lock);return out;
}
static void release(packet_t *packet)
{portENTER_CRITICAL(&lock);packet->used=false;portEXIT_CRITICAL(&lock);}
static void drop(void){portENTER_CRITICAL(&lock);++dropped;portEXIT_CRITICAL(&lock);}
static bool active(void)
{portENTER_CRITICAL(&lock);bool enabled=accepting;portEXIT_CRITICAL(&lock);return enabled;}
static esp_err_t transmit(void *ctx,void *buffer,size_t length)
{
    (void)ctx;
    if(!active() || length>FRAME_MAX)return ESP_ERR_INVALID_STATE;
    packet_t *packet=take(tx_pool);if(!packet){drop();return ESP_ERR_NO_MEM;}
    packet->len=length;memcpy(packet->data,buffer,length);
    if(xQueueSend(tx_queue,&packet,0)!=pdTRUE){release(packet);drop();return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
static void free_rx(void *ctx,void *buffer)
{
    (void)ctx;
    for(unsigned i=0;i<PACKETS;++i)if(buffer==rx_pool[i].data){release(&rx_pool[i]);return;}
}
bool tud_network_recv_cb(const uint8_t *buffer,uint16_t length)
{
    if(active() && usb_netif && length>=14 && length<=FRAME_MAX){
        packet_t *packet=take(rx_pool);
        if(packet){
            memcpy(packet->data,buffer,length);
            esp_err_t error=esp_netif_receive(usb_netif,packet->data,length,packet->data);
            // ethernetif_input consumes ownership on both success and failure.
            if(error!=ESP_OK)drop();
            else{portENTER_CRITICAL(&lock);++rx_count;portEXIT_CRITICAL(&lock);}
        }else drop();
    }else drop();
    tud_network_recv_renew();return true;
}
uint16_t tud_network_xmit_cb(uint8_t *destination,void *reference,uint16_t length)
{
    packet_t *packet=reference;
    if(length!=packet->len || length>FRAME_MAX)return 0;
    memcpy(destination,packet->data,length);return length;
}
void tud_network_init_cb(void){}
bool tud_network_default_link_state_cb(void){return false;}
static void pump(void *unused)
{
    (void)unused;
    if(tud_ready())tud_network_link_state(TINYUSB_PORT_HIGH_SPEED_0,active());
    for(unsigned i=0;i<PACKETS;++i){
        if(!pending_tx && xQueueReceive(tx_queue,&pending_tx,0)!=pdTRUE)break;
        if(!active() || !tud_ready()){release(pending_tx);pending_tx=NULL;drop();continue;}
        if(!tud_network_can_xmit(pending_tx->len))break;
        // NCM copies the entire frame synchronously through xmit_cb.
        tud_network_xmit(pending_tx,pending_tx->len);release(pending_tx);pending_tx=NULL;
        portENTER_CRITICAL(&lock);++tx_count;portEXIT_CRITICAL(&lock);
    }
    portENTER_CRITICAL(&lock);pump_pending=false;portEXIT_CRITICAL(&lock);
}
esp_err_t ouse_usb_network_start(void)
{
    tx_queue=xQueueCreate(PACKETS,sizeof(packet_t *));
    return tx_queue ? ESP_OK : ESP_ERR_NO_MEM;
}
static esp_err_t create_netif(void)
{
    if(!rx_pool)rx_pool=heap_caps_calloc(PACKETS,sizeof(packet_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!tx_pool)tx_pool=heap_caps_calloc(PACKETS,sizeof(packet_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!rx_pool || !tx_pool)return ESP_ERR_NO_MEM;
    uint8_t mac[6];esp_read_mac(mac,ESP_MAC_BASE);mac[0]=(mac[0]|2)&0xfe;mac[5]^=0x81;
    IP4_ADDR(&local_ip.ip,192,168,77,1);local_ip.gw=local_ip.ip;
    IP4_ADDR(&local_ip.netmask,255,255,255,0);
    esp_netif_inherent_config_t inherent={.flags=ESP_NETIF_DHCP_SERVER|ESP_NETIF_FLAG_AUTOUP,
        .ip_info=&local_ip,.if_key="OUSE_USB",.if_desc="usb",.route_prio=1,.mtu=1500};
    memcpy(inherent.mac,mac,6);
    // ESP-IDF requires a non-NULL driver identity even for a virtual NIC.
    esp_netif_driver_ifconfig_t driver={.handle=&usb_netif,.transmit=transmit,.driver_free_rx_buffer=free_rx};
    esp_netif_config_t config={.base=&inherent,.driver=&driver,.stack=ESP_NETIF_NETSTACK_DEFAULT_ETH};
    usb_netif=esp_netif_new(&config);return usb_netif ? ESP_OK : ESP_ERR_NO_MEM;
}
void ouse_usb_network_poll(bool enabled)
{
    // Pump at 20 ms, inspect/configure IP only four times a second.
    bool schedule=false;
    portENTER_CRITICAL(&lock);
    if(!pump_pending){pump_pending=true;schedule=true;}
    if(!enabled)accepting=false;
    portEXIT_CRITICAL(&lock);
    if(schedule)usbd_defer_func(pump,NULL,false);
    int64_t now=esp_timer_get_time();
    if(now<next_sample && (enabled || (!netif_up && !napt_on)))return;
    next_sample=now+250000;
    if(enabled && !usb_netif){
        esp_err_t error=create_netif();
        if(error!=ESP_OK){portENTER_CRITICAL(&lock);last_error=error;portEXIT_CRITICAL(&lock);return;}
    }
    ouse_wifi_state_t wifi={0};ouse_wifi_get(&wifi);
    esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t remote={0};
    bool connected=sta && wifi.connected && esp_netif_get_ip_info(sta,&remote)==ESP_OK && remote.ip.addr;
    uint32_t mask=remote.netmask.addr & local_ip.netmask.addr;
    bool overlap=connected && (remote.ip.addr&mask)==(local_ip.ip.addr&mask);
    bool up=enabled && tud_ready() && connected && !overlap;
    esp_err_t error=ESP_OK;
    if(usb_netif && (!up || last_uplink!=remote.ip.addr) && (netif_up || napt_on)){
        portENTER_CRITICAL(&lock);accepting=false;portEXIT_CRITICAL(&lock);
        if(napt_on){error=esp_netif_napt_disable(usb_netif);if(error==ESP_OK)napt_on=false;}
        if(netif_up){esp_netif_action_stop(usb_netif,NULL,0,NULL);netif_up=esp_netif_is_netif_up(usb_netif);}
    }
    if(usb_netif && up && !netif_up && error==ESP_OK){
        esp_netif_dns_info_t dns={0};
        error=esp_netif_get_dns_info(sta,ESP_NETIF_DNS_MAIN,&dns);
        if(error==ESP_OK && dns.ip.u_addr.ip4.addr){
            error=esp_netif_set_dns_info(usb_netif,ESP_NETIF_DNS_MAIN,&dns);
            uint8_t offer_dns=2;
            if(error==ESP_OK)error=esp_netif_dhcps_option(usb_netif,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&offer_dns,sizeof(offer_dns));
        }
        if(error==ESP_OK){
            esp_netif_action_start(usb_netif,NULL,0,NULL);
            netif_up=esp_netif_is_netif_up(usb_netif);
            if(!netif_up)error=ESP_ERR_INVALID_STATE;
            else{
                esp_netif_dhcp_status_t status;
                error=esp_netif_dhcps_get_status(usb_netif,&status);
                if(error==ESP_OK && status!=ESP_NETIF_DHCP_STARTED)error=ESP_ERR_INVALID_STATE;
                if(error==ESP_OK){error=esp_netif_napt_enable(usb_netif);napt_on=error==ESP_OK;}
            }
        }
        if(error!=ESP_OK && netif_up){esp_netif_action_stop(usb_netif,NULL,0,NULL);netif_up=false;}
        last_uplink=remote.ip.addr;
    }
    portENTER_CRITICAL(&lock);accepting=up && netif_up && napt_on;
    cleanup_pending=!up && (netif_up || napt_on);
    uplink=connected;conflict=overlap;last_error=error;portEXIT_CRITICAL(&lock);
}
void ouse_usb_network_snapshot(ouse_usb_state_t *out)
{
    portENTER_CRITICAL(&lock);out->network_up=accepting;out->uplink=uplink;
    out->network_stopping=cleanup_pending;
    out->subnet_conflict=conflict;out->network_error=last_error;
    out->rx_packets=rx_count;out->tx_packets=tx_count;out->dropped_packets=dropped;
    portEXIT_CRITICAL(&lock);strcpy(out->ip,"192.168.77.1");
}
