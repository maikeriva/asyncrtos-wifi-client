/**
 * @file main.c
 * @author Michele Riva (micheleriva@protonmail.com)
 * @brief AOS WiFi client example 0
 * @version 0.9.0
 * @date 2023-04-17
 *
 * @copyright Copyright (c) 2023
 *
 * This basic example shows how to initialize the WiFi client, start it, scan, connect, disconnect, and stop it.
 */
#include <stdio.h>
#include <aos.h>
#include <aos_wifi_client.h>
#include <esp_netif.h>
#include <sdkconfig.h>

static const char *_test_ssid = "MY_SSID";
static const char *_test_password = "MY_PASSWORD";
static aos_wifi_client_scan_result_t _results[10] = {};

static void wifi_event_handler(aos_wifi_client_event_t event, void *args)
{
    printf("Event:%d\n", event);
}

void app_main(void)
{
    // Initialize ESP netif
    esp_netif_init();

    // Initialize AOS WiFi client
    // All fields are mandatory, we want to be explicit
    aos_wifi_client_config_t config = {
        .connection_attempts = UINT32_MAX,
        .reconnection_attempts = UINT32_MAX,
        .event_handler = wifi_event_handler};
    aos_wifi_client_init(&config);

    // Start the client for example with an awaitable future
    aos_future_t *start = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_start)(0);
    aos_await(aos_wifi_client_start(start));
    aos_awaitable_free(start);

    // Scan for networks
    aos_future_t *scan = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_scan)(_results, 10, 0, 0);
    aos_await(aos_wifi_client_scan(scan));
    AOS_ARGS_T(aos_wifi_client_scan) *scan_args = aos_args_get(scan);
    for (size_t i = 0; i < scan_args->out_results_count; i++)
    {
        printf("Scan result (ssid:%s, strength:%f, open:%u)\n", _results[i].ssid, _results[i].strength, _results[i].open);
    }
    aos_awaitable_free(scan);

    // Connect to a network
    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_connect)(_test_ssid, _test_password, 0);
    aos_await(aos_wifi_client_connect(connect));
    aos_awaitable_free(connect);

    // Disconnect from network
    aos_future_t *disconnect = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_disconnect)();
    aos_await(aos_wifi_client_disconnect(disconnect));
    aos_awaitable_free(disconnect);

    // Stop WiFi client
    aos_future_t *stop = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_stop)();
    aos_await(aos_wifi_client_stop(stop));
    aos_awaitable_free(stop);
}
