#include "PGM_Mdns.h"

static const char *TAG = "mdns";

static char *generate_hostname(void)
{
#ifndef CONFIG_MDNS_ADD_MAC_TO_HOSTNAME
    return strdup("watervisionMDF");
#else
    uint8_t mac[6];
    char *hostname;
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (-1 == asprintf(&hostname, "%s-%02X%02X%02X", CONFIG_MDNS_HOSTNAME, mac[3], mac[4], mac[5]))
    {
        abort();
    }
    return hostname;
#endif
}

void initialise_mdns(void)
{
    char *hostname = generate_hostname();

    // initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    // set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK(mdns_hostname_set(hostname));
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);
    // set default mDNS instance name
    ESP_ERROR_CHECK(mdns_instance_name_set("ESP32 with mDNS"));

    // structure with TXT records
    mdns_txt_item_t serviceTxtData[3] = {
        {"board", "esp32"},
        {"u", "user"},
        {"p", "password"}};

    // initialize service
    ESP_ERROR_CHECK(mdns_service_add("ESP32-WebServer", "_http", "_tcp", 8080, serviceTxtData, 3));
    ESP_ERROR_CHECK(mdns_service_subtype_add_for_host("ESP32-WebServer", "_http", "_tcp", NULL, "_server"));
#if CONFIG_MDNS_MULTIPLE_INSTANCE
    // ESP_ERROR_CHECK(mdns_service_add("ESP32-WebServer1", "_http", "_tcp", 80, NULL, 0));
#endif

#if CONFIG_MDNS_PUBLISH_DELEGATE_HOST
    char *delegated_hostname;
    if (-1 == asprintf(&delegated_hostname, "%s-delegated", hostname))
    {
        abort();
    }

    mdns_ip_addr_t addr4, addr6;
    esp_netif_str_to_ip4("10.0.0.1", &addr4.addr.u_addr.ip4);
    addr4.addr.type = ESP_IPADDR_TYPE_V4;
    esp_netif_str_to_ip6("fd11:22::1", &addr6.addr.u_addr.ip6);
    addr6.addr.type = ESP_IPADDR_TYPE_V6;
    addr4.next = &addr6;
    addr6.next = NULL;
    ESP_ERROR_CHECK(mdns_delegate_hostname_add(delegated_hostname, &addr4));
    ESP_ERROR_CHECK(mdns_service_add_for_host("test0", "_http", "_tcp", delegated_hostname, 1234, serviceTxtData, 3));
    free(delegated_hostname);
#endif // CONFIG_MDNS_PUBLISH_DELEGATE_HOST

    // add another TXT item
    mdns_service_txt_item_set("_http", "_tcp", "path", "/ws");
    // change TXT item value
    ESP_ERROR_CHECK(mdns_service_txt_item_set_with_explicit_value_len("_http", "_tcp", "u", "admin", strlen("admin")));
    free(hostname);
}

void PGM_Mdns_Init(void)
{
    ESP_LOGI(TAG, "mDNS Ver: %s", ESP_MDNS_VERSION_NUMBER);

    initialise_mdns();
}