#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_netif.h"

#include "driver/gpio.h"
#include "debug/dbg.h"
#include "net/ipnet.h"
#include <net/ethernet_setup.hh>
#include "stdint.h"
#include "main_loop/main_queue.hh"
#include "debug/log.h"

#ifdef CONFIG_NETWORK_DEBUG
#define D(x) x
#else
#define D(x)
#endif
#define logtag "net"

extern esp_ip4_addr_t ip4_address, ip4_gateway_address, ip4_netmask;

extern "C" esp_eth_phy_t* my_esp_eth_phy_new_lan8720(const eth_phy_config_t *config);
static esp_eth_phy_t* (*ethernet_create_phy)(const eth_phy_config_t *config);

#define DX(x) x

static int ethernet_phy_power_pin = -1;
static esp_netif_t *eth_netif;
static esp_eth_mac_t *mac;
static esp_eth_phy_t *phy;
static esp_eth_handle_t s_eth_handle;
static esp_eth_netif_glue_handle_t eth_netif_glue;
static esp_event_handler_instance_t eth_event_handler_instance;
static esp_event_handler_instance_t got_ip_handler_instance;

static const char *TAG = "ethernet";

/**
 * @note RMII data pins are fixed in esp32:
 * TXD0 <=> GPIO19
 * TXD1 <=> GPIO22
 * TX_EN <=> GPIO21
 * RXD0 <=> GPIO25
 * RXD1 <=> GPIO26
 * CLK <=> GPIO0
 *
 */

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  switch (event_id) {
  case ETHERNET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "Ethernet Link Up");
    break;
  case ETHERNET_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "Ethernet Link Down");
    if (ipnet_lostIpAddr_cb)
      ipnet_lostIpAddr_cb();
    break;
  case ETHERNET_EVENT_START:
    ESP_LOGI(TAG, "Ethernet Started");
    break;
  case ETHERNET_EVENT_STOP:
    ESP_LOGI(TAG, "Ethernet Stopped");
    break;
  default:
    break;
  }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  ip_event_got_ip_t *event = (ip_event_got_ip_t*) event_data;
  const esp_netif_ip_info_t *ip_info = &event->ip_info;

  ESP_LOGI(TAG, "Ethernet Got IP Address");
  ESP_LOGI(TAG, "~~~~~~~~~~~");
  ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
  ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
  ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
  ESP_LOGI(TAG, "~~~~~~~~~~~");

  ip4_address = ip_info->ip;
  ip4_gateway_address = ip_info->gw;
  ip4_netmask = ip_info->netmask;

  if (ipnet_gotIpAddr_cb)
    ipnet_gotIpAddr_cb();

  mainLoop_callFun(ipnet_connected);
}

static void ethernet_switch_phy_power(bool on = false) {
  if (ethernet_phy_power_pin >= 0) {
    ESP_LOGI(TAG, "Power %s PHY using gpio %d", on ? "on" : "off", ethernet_phy_power_pin);

    esp_rom_gpio_pad_select_gpio(ethernet_phy_power_pin);
    gpio_set_direction(static_cast<gpio_num_t>(ethernet_phy_power_pin), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(ethernet_phy_power_pin), 1);
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

bool ethernet_setdown() {
  if (s_eth_handle) {

    if (auto ec = esp_eth_stop(s_eth_handle); ec != ESP_OK) {
      ESP_LOGE(TAG, "stopping ethernet driver failed: %s", esp_err_to_name(ec));
    }

    // unregister handlers
    if (eth_event_handler_instance) {
      if (auto ec = esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler_instance); ec != ESP_OK) {
        ESP_LOGE(TAG, "unregistering eth-event-handler failed: %s", esp_err_to_name(ec));
      } else
        eth_event_handler_instance = 0;
    }
    if (got_ip_handler_instance) {
      if (auto ec = esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_handler_instance); ec != ESP_OK) {
        ESP_LOGE(TAG, "unregistering got-ip-handler failed: %s", esp_err_to_name(ec));
      } else
        got_ip_handler_instance = 0;
    }

    if (auto ec = esp_eth_del_netif_glue(eth_netif_glue); ec != ESP_OK) {
      ESP_LOGE(TAG, "deleting netif_glue failed: %s", esp_err_to_name(ec));
    } else
      eth_netif_glue = 0;

    if (eth_netif) {
      esp_netif_destroy(eth_netif);
      eth_netif = 0;
    }
    esp_netif_deinit();

    if (auto ec = esp_eth_driver_uninstall(s_eth_handle); ec != ESP_OK) {
      ESP_LOGE(TAG, "driver un-install failed: %s", esp_err_to_name(ec));
      return false;
    }
    s_eth_handle = 0;
  }

  if (mac) {
    mac->del(mac);
    mac = 0;
  }

  if (phy) {
    phy->del(phy);
    phy = 0;
  }
  // power-down phy here
  ethernet_switch_phy_power(false);

  return true;
}

bool ethernet_setup(struct cfg_lan *cfg_lan) {

  ethernet_setdown();

  {
    // Configure MAC and PHY
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    // Board specific configurations
    switch (cfg_lan->phy) {

    case lanBoardOlimexEsp32Gateway:
      esp32_emac_config.smi_gpio.mdc_num = 23;
      esp32_emac_config.smi_gpio.mdio_num = 18;
      esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
      esp32_emac_config.clock_config.rmii.clock_gpio = EMAC_CLK_OUT_180_GPIO;
      ethernet_create_phy = esp_eth_phy_new_lan87xx;
      phy_config.phy_addr = 0;
      phy_config.reset_gpio_num = -1;
      ethernet_phy_power_pin = 5;
      ESP_LOGI(TAG, "board=%s, phy=%s, clock_mode=%d, clock_gpio=%d, phy_addr=%ld, reset_gpio=%d, phy_power_gpio=%d", "Olimex-ESP32-Gateway", "LAN87xx",
          esp32_emac_config.clock_config.rmii.clock_mode, esp32_emac_config.clock_config.rmii.clock_gpio, phy_config.phy_addr, phy_config.reset_gpio_num,
          ethernet_phy_power_pin);
      break;

    case lanBoardOlimexEsp32Poe:
      esp32_emac_config.smi_gpio.mdc_num = 23;
      esp32_emac_config.smi_gpio.mdio_num = 18;
      esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
      esp32_emac_config.clock_config.rmii.clock_gpio = EMAC_CLK_OUT_180_GPIO;
      ethernet_create_phy = esp_eth_phy_new_lan87xx;
      phy_config.phy_addr = 0;
      phy_config.reset_gpio_num = -1;
      ethernet_phy_power_pin = 12;
      ESP_LOGI(TAG, "board=%s, phy=%s, clock_mode=%d, clock_gpio=%d, phy_addr=%ld, reset_gpio=%d, phy_power_gpio=%d", "Olimex-ESP32-POE", "LAN87xx",
          esp32_emac_config.clock_config.rmii.clock_mode, esp32_emac_config.clock_config.rmii.clock_gpio, phy_config.phy_addr, phy_config.reset_gpio_num,
          ethernet_phy_power_pin);
      break;

    case lanBoardWt32Eth01:
      esp32_emac_config.smi_gpio.mdc_num = 23;
      esp32_emac_config.smi_gpio.mdio_num = 18;
      esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
      esp32_emac_config.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO;
      ethernet_create_phy = esp_eth_phy_new_lan87xx;
      phy_config.phy_addr = 1;
      phy_config.reset_gpio_num = -1;
      ethernet_phy_power_pin = 16;
      ESP_LOGI(TAG, "board=%s, phy=%s, clock_mode=%d, clock_gpio=%d, phy_addr=%ld, reset_gpio=%d, phy_power_gpio=%d", "wt32-eth01", "LAN87xx",
          esp32_emac_config.clock_config.rmii.clock_mode, esp32_emac_config.clock_config.rmii.clock_gpio, phy_config.phy_addr, phy_config.reset_gpio_num,
          ethernet_phy_power_pin);
      break;

    case lanBoardTInternetCom:
      esp32_emac_config.smi_gpio.mdc_num = 23;
      esp32_emac_config.smi_gpio.mdio_num = 18;
      esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
      esp32_emac_config.clock_config.rmii.clock_gpio = EMAC_APPL_CLK_OUT_GPIO;
      ethernet_create_phy = esp_eth_phy_new_lan87xx;
      phy_config.phy_addr = 0;
      phy_config.reset_gpio_num = 5;
      ethernet_phy_power_pin = 4;
      ESP_LOGI(TAG, "board=%s, phy=%s, clock_mode=%d, clock_gpio=%d, phy_addr=%ld, reset_gpio=%d, phy_power_gpio=%d", "T-Internet-Com", "LAN87xx",
          esp32_emac_config.clock_config.rmii.clock_mode, esp32_emac_config.clock_config.rmii.clock_gpio, phy_config.phy_addr, phy_config.reset_gpio_num,
          ethernet_phy_power_pin);
      break;

    case lanPhyRTL8201:
      ethernet_create_phy = esp_eth_phy_new_rtl8201;
      ethernet_phy_power_pin = cfg_lan->pwr_gpio;
      break;

    case lanPhyIP101:
      ethernet_create_phy = esp_eth_phy_new_ip101;
      ethernet_phy_power_pin = cfg_lan->pwr_gpio;
      break;

    case lanPhyLAN8720:
    default:
      ethernet_create_phy = esp_eth_phy_new_lan87xx;
      phy_config.phy_addr = CONFIG_NET_ETH_PHY_ADDR;
      phy_config.reset_gpio_num = -1;
      ethernet_phy_power_pin = cfg_lan->pwr_gpio;
      ESP_LOGI(TAG, "board=%s, phy=%s, clock_mode=%d, clock_gpio=%d, phy_addr=%ld, reset_gpio=%d, phy_power_gpio=%d", "esp32-???", "LAN87xx",
          esp32_emac_config.clock_config.rmii.clock_mode, esp32_emac_config.clock_config.rmii.clock_gpio, phy_config.phy_addr, phy_config.reset_gpio_num,
          ethernet_phy_power_pin);
    }

    // power-on phy here
    ethernet_switch_phy_power(true);

    // Setup MAC
    if (mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config); !mac) {
      ESP_LOGE(TAG, "setup MAC failed");
      goto error;
    }
    // Setup PHY
    if (phy = ethernet_create_phy(&phy_config); !phy) {
      ESP_LOGE(TAG, "setup PHY failed");
      goto error;
    }
  }
  {
    // Install Ethernet Driver
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    if (auto ec = esp_eth_driver_install(&config, &s_eth_handle); ec != ESP_OK) {
      ESP_LOGE(TAG, "driver install failed: %s", esp_err_to_name(ec));
      goto error;
    }
  }
  {
    // create netif
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    if (eth_netif = esp_netif_new(&cfg); !eth_netif) {
      ESP_LOGE(TAG, "creating new netif failed");
      goto error;
    }
  }
  {
    eth_netif_glue = esp_eth_new_netif_glue(s_eth_handle);
    /* attach Ethernet driver to TCP/IP stack */
    if (auto ec = esp_netif_attach(eth_netif, eth_netif_glue); ec != ESP_OK) {
      ESP_LOGE(TAG, "attaching driver to tcp/ip-stack failed: %s", esp_err_to_name(ec));
      goto error;
    }
  }
  // register handlers
  if (auto ec = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL, &eth_event_handler_instance); ec != ESP_OK) {
    ESP_LOGE(TAG, "registering eth-event-handler failed: %s", esp_err_to_name(ec));
    goto error;
  }
  if (auto ec = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL, &got_ip_handler_instance); ec != ESP_OK) {
    ESP_LOGE(TAG, "registering got-ip-handler failed: %s", esp_err_to_name(ec));
    goto error;
  }
  {
    /* start Ethernet driver state machine */
    if (auto ec = esp_eth_start(s_eth_handle); ec != ESP_OK) {
      ESP_LOGE(TAG, "starting state machine failed: %s", esp_err_to_name(ec));
      goto error;
    }
  }
  ESP_LOGI(TAG, "Ethernet start was successful");
  return true;

  error: ethernet_setdown();
  ESP_LOGE(TAG, "Ethernet setup or start did not succeed");
  return false;
}

