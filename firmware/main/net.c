//
// Network for the Sedona VM: Wi-Fi station, OpenCores Ethernet (QEMU) or none
// (menuconfig -> Sedona VM -> Network).  Logs the address when it is up.
//
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "sdkconfig.h"
#include "net.h"
#if CONFIG_SEDONA_NET_WIFI
#include "esp_wifi.h"
#endif
#if CONFIG_SEDONA_NET_OPENETH || CONFIG_SEDONA_NET_P4_ETH
#include "esp_eth.h"
#endif

#if CONFIG_SEDONA_NET_P4_ETH
#include "esp_eth_mac_esp.h"
#include "esp_eth_phy_ip101.h"
#endif

static const char* TAG = "net";

static void onGotIp(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
  ESP_LOGI(TAG, "address " IPSTR ", SOX udp/1876", IP2STR(&e->ip_info.ip));
  static bool ntpStarted;
  if (!ntpStarted && strlen(CONFIG_SEDONA_NTP_SERVER)) {
    esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SEDONA_NTP_SERVER);
    if (esp_netif_sntp_init(&cfg)==ESP_OK)ntpStarted=true;
  }
}

#if CONFIG_SEDONA_NET_WIFI
static void onWifi(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  if (id == WIFI_EVENT_STA_START || id == WIFI_EVENT_STA_DISCONNECTED)
    esp_wifi_connect();       // and keep retrying while the AP is away
}

static void wifiStart(void)
{
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  wifi_config_t cfg = { 0 };
  esp_netif_create_default_wifi_sta();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifi, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onGotIp, NULL);
  strlcpy((char*)cfg.sta.ssid, CONFIG_SEDONA_WIFI_SSID, sizeof(cfg.sta.ssid));
  strlcpy((char*)cfg.sta.password, CONFIG_SEDONA_WIFI_PASSWORD, sizeof(cfg.sta.password));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  esp_wifi_set_ps(WIFI_PS_NONE);       // no power save: stable SOX latency
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "Wi-Fi station, SSID '%s'", CONFIG_SEDONA_WIFI_SSID);
}
#endif

#if CONFIG_SEDONA_NET_OPENETH
static void openethStart(void)
{
  eth_mac_config_t macCfg = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phyCfg = ETH_PHY_DEFAULT_CONFIG();
  esp_eth_handle_t eth = NULL;
  esp_netif_config_t netifCfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t* netif = esp_netif_new(&netifCfg);
  phyCfg.autonego_timeout_ms = 100;
  esp_eth_mac_t* mac = esp_eth_mac_new_openeth(&macCfg);
  esp_eth_phy_t* phy = esp_eth_phy_new_dp83848(&phyCfg);
  esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
  ESP_ERROR_CHECK(esp_eth_driver_install(&cfg, &eth));
  ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(eth)));
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, onGotIp, NULL);
  ESP_ERROR_CHECK(esp_eth_start(eth));
  ESP_LOGI(TAG, "OpenCores Ethernet (QEMU)");
}
#endif


#if CONFIG_SEDONA_NET_P4_ETH
static void onEthernet(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  if (id == ETHERNET_EVENT_CONNECTED) ESP_LOGI(TAG, "Ethernet link UP");
  else if (id == ETHERNET_EVENT_DISCONNECTED) ESP_LOGW(TAG, "Ethernet link DOWN");
}

static void p4EthernetStart(void)
{
  // Waveshare ESP32-P4-ETH: IP101GRI, address 1, reset 51.
  // IDF P4 RMII defaults match the board: MDC31, MDIO52, REF_CLK50,
  // TX_EN49, TXD0/1 34/35, CRS_DV28, RXD0/1 29/30.
  eth_mac_config_t macCfg = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phyCfg = ETH_PHY_DEFAULT_CONFIG();
  eth_esp32_emac_config_t emacCfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
  phyCfg.phy_addr = 1;
  phyCfg.reset_gpio_num = 51;
  esp_eth_mac_t* mac = esp_eth_mac_new_esp32(&emacCfg, &macCfg);
  esp_eth_phy_t* phy = esp_eth_phy_new_ip101(&phyCfg);
  ESP_ERROR_CHECK(mac && phy ? ESP_OK : ESP_ERR_NO_MEM);
  esp_eth_handle_t eth = NULL;
  esp_eth_config_t cfg = ETH_DEFAULT_CONFIG(mac, phy);
  ESP_ERROR_CHECK(esp_eth_driver_install(&cfg, &eth));
  esp_netif_config_t netifCfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t* netif = esp_netif_new(&netifCfg);
  ESP_ERROR_CHECK(netif ? ESP_OK : ESP_ERR_NO_MEM);
  ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(eth)));
  ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, onEthernet, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, onGotIp, NULL));
  if (strlen(CONFIG_SEDONA_ETH_STATIC_IP)) {
    esp_netif_ip_info_t ip = {0};
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(CONFIG_SEDONA_ETH_STATIC_IP, &ip.ip));
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(CONFIG_SEDONA_ETH_GATEWAY, &ip.gw));
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(CONFIG_SEDONA_ETH_NETMASK, &ip.netmask));
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip));
    esp_netif_dns_info_t dns={0};
    dns.ip.type=ESP_IPADDR_TYPE_V4;dns.ip.u_addr.ip4.addr=ip.gw.addr;
    ESP_ERROR_CHECK(esp_netif_set_dns_info(netif,ESP_NETIF_DNS_MAIN,&dns));
  }
  ESP_ERROR_CHECK(esp_eth_start(eth));
  ESP_LOGI(TAG, "Waveshare ESP32-P4-ETH / IP101, IPv4 configured");
}
#endif

void netStart(void)
{
  // SOX/web still create sockets in the bring-up profile without a physical NIC.
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
#if CONFIG_SEDONA_NET_NONE
  ESP_LOGI(TAG, "physical network disabled (TCP/IP stack initialized)");
#else
#if CONFIG_SEDONA_NET_WIFI
  wifiStart();
#elif CONFIG_SEDONA_NET_OPENETH
  openethStart();
#elif CONFIG_SEDONA_NET_P4_ETH
  p4EthernetStart();
#endif
#endif
}
