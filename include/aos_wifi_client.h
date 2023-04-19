/**
 * @file aos_wifi_client.h
 * @author Michele Riva (michele.riva@protonmail.com)
 * @brief AOS WiFi Client API
 * @version 0.9.0
 * @date 2023-04-18
 * 
 * @copyright Copyright (c) 2023
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless futureuired by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#include <aos.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief WiFi client events
     */
    typedef enum aos_wifi_client_event_t
    {
        AOS_WIFI_CLIENT_EVENT_RECONNECTING, // WiFi client is reconnecting
        AOS_WIFI_CLIENT_EVENT_RECONNECTED,  // WiFi client reconnected successfully
        AOS_WIFI_CLIENT_EVENT_DISCONNECTED, // WiFi client disconnected unexpectedly
    } aos_wifi_client_event_t;

    /**
     * @brief WiFi client configuration
     *
     * @warning The event_handler element is mandatory!
     */
    typedef struct aos_wifi_client_config_t
    {
        unsigned int connection_attempts;                                 // Number of connection attempts before giving up
        unsigned int reconnection_attempts;                               // Number or recovery attempts before giving up
        void (*event_handler)(aos_wifi_client_event_t event, void *args); // Event handler, will receive notifications of unexpected WiFi events
    } aos_wifi_client_config_t;

    /**
     * @brief Initialize WiFi client
     *
     * To be called once before anything else. Is idempotent, but only the configuration given the first time is used.
     *
     * @param config WiFi client config
     */
    void aos_wifi_client_init(aos_wifi_client_config_t *config);

    AOS_DECLARE(aos_wifi_client_start, unsigned int out_err)
    /**
     * @brief Start WiFi client
     *
     * @param future Future
     * @param out_err (on future) 0 if success, 1 otherwise
     * @return aos_future_t* Same future as input
     */
    aos_future_t *aos_wifi_client_start(aos_future_t *future);

    AOS_DECLARE(aos_wifi_client_stop)
    /**
     * @brief Stops WiFi client
     *
     * @param future Future
     * @return aos_future_t* Same future as input
     */
    aos_future_t *aos_wifi_client_stop(aos_future_t *future);

    AOS_DECLARE(aos_wifi_client_connect, const char *in_ssid, const char *in_password, unsigned int out_err)
    /**
     * @brief Connect to a given WiFi network.
     *
     * @note In case of multiple consecutive calls, futures not yet resolved will be resolved with out_err = 1.
     *
     * @param future Future
     * @param in_ssid (on future) SSID
     * @param in_password (on future) Password (if any)
     * @param out_err (on future) 0 if success, 1 otherwise
     * @return aos_future_t* Same future as input
     */
    aos_future_t *aos_wifi_client_connect(aos_future_t *future);

    AOS_DECLARE(aos_wifi_client_disconnect)
    /**
     * @brief Disconnect from current WiFi network (if any)
     *
     * @param future Future
     * @return aos_future_t* Same future as input
     */
    aos_future_t *aos_wifi_client_disconnect(aos_future_t *future);

    /**
     * @brief Scan result entry
     */
    typedef struct aos_wifi_client_scan_result_t
    {
        char ssid[34];  // SSID
        float strength; // Signal strength on a 0-1 scale, higher is better
        bool open;      // Whether network is open or requires password
    } aos_wifi_client_scan_result_t;
    AOS_DECLARE(aos_wifi_client_scan, aos_wifi_client_scan_result_t *in_results, size_t in_results_size, size_t out_results_count, uint32_t out_err)
    /**
     * @brief Scan for available networks
     *
     * @param future Future
     * @param in_results (on future) Pre-allocated on-heap structure to allocate results
     * @param in_results_size (on future) Number of slots in in_results structure
     * @param out_results_count (on future) Number of results
     * @param out_err (on future) 0 if success, 1 otherwise. Note that the ESP WiFi driver cannot scan while connecting to a network.
     * @return aos_future_t* Same future as input
     */
    aos_future_t *aos_wifi_client_scan(aos_future_t *future);

#ifdef __cplusplus
}
#endif