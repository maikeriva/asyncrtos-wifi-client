/**
 * @file aos_wifi_client.c
 * @author Michele Riva (michele.riva@protonmail.com)
 * @brief AOS WiFi client implementation
 * @version 0.1
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
 *
 * TODO:
 * - Ensure country info and channels are handled automatically (there's some logic already behind the hood)
 * - Could be interesting to use esp_wifi_set_event_mask(uint32_t mask) to save some calls from events
 */
#include <aos_wifi_client.h>
#include <string.h>
#include <esp_wifi.h>
#include <sdkconfig.h>
#ifdef CONFIG_AOS_WIFI_CLIENT_LOG_NONE
#define LOG_LOCAL_LEVEL ESP_LOG_NONE
#elif CONFIG_AOS_WIFI_CLIENT_LOG_ERROR
#define LOG_LOCAL_LEVEL ESP_LOG_ERROR
#elif CONFIG_AOS_WIFI_CLIENT_LOG_WARN
#define LOG_LOCAL_LEVEL ESP_LOG_WARN
#elif CONFIG_AOS_WIFI_CLIENT_LOG_INFO
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#elif CONFIG_AOS_WIFI_CLIENT_LOG_DEBUG
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#elif CONFIG_AOS_WIFI_CLIENT_LOG_VERBOSE
#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#endif
#include <esp_log.h>

typedef enum
{
    AOS_WIFI_CLIENT_EVT_CONFIG_SET,
    AOS_WIFI_CLIENT_EVT_CONNECT,
    AOS_WIFI_CLIENT_EVT_DISCONNECT,
    AOS_WIFI_CLIENT_EVT_SCAN,
    AOS_WIFI_CLIENT_EVT_CONNECTED,
    AOS_WIFI_CLIENT_EVT_DISCONNECTED,
    AOS_WIFI_CLIENT_EVT_SCANDONE
} _aos_wifi_client_evt_t;

typedef enum
{
    AOS_WIFI_CLIENT_STATE_DISCONNECTED,
    AOS_WIFI_CLIENT_STATE_CONNECTING,
    AOS_WIFI_CLIENT_STATE_CONNECTED,
    AOS_WIFI_CLIENT_STATE_RECONNECTING,
} _aos_wifi_client_state_t;

typedef struct _aos_wifi_client_ctx_t
{
    aos_wifi_client_config_t config;
    _aos_wifi_client_state_t state;
    esp_netif_t *netif;
    esp_event_handler_instance_t ip_handler_instance;
    esp_event_handler_instance_t wifi_handler_instance;
    aos_future_t *connect_future;
    aos_future_t *scan_future;
    esp_netif_ip_info_t *ip_info;
    unsigned int connection_attempt;
    unsigned int reconnection_attempt;
} _aos_wifi_client_ctx_t;

static uint32_t _aos_wifi_client_onstart(aos_task_t *task, aos_future_t *future);
static uint32_t _aos_wifi_client_onstop(aos_task_t *task, aos_future_t *future);
static void _aos_wifi_client_connect_handler(aos_task_t *task, aos_future_t *future);
static void _aos_wifi_client_disconnect_handler(aos_task_t *task, aos_future_t *future);
static void _aos_wifi_client_onconnected_handler(aos_task_t *task, aos_future_t *future);
static void _aos_wifi_client_ondisconnected_handler(aos_task_t *task, aos_future_t *future);
static void _aos_wifi_client_scan_handler(aos_task_t *task, aos_future_t *future);
static void _aos_wifi_client_onscandone_handler(aos_task_t *task, aos_future_t *future);
static void _aos_wifi_client_disconnect(aos_task_t *task);
static void _aos_wifi_client_stopcurrentscan(aos_task_t *task);
static void _aos_wifi_client_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

static aos_task_t *_task = NULL;
static const char *_tag = "AOS WiFi client";

void aos_wifi_client_init(aos_wifi_client_config_t *config)
{
    if (_task)
        return;

    _aos_wifi_client_ctx_t *ctx = calloc(1, sizeof(_aos_wifi_client_ctx_t));
    aos_task_config_t task_config = {
        .stacksize = CONFIG_AOS_WIFI_CLIENT_TASK_STACKSIZE,
        .queuesize = CONFIG_AOS_WIFI_CLIENT_TASK_QUEUESIZE,
        .priority = CONFIG_AOS_WIFI_CLIENT_TASK_PRIORITY,
        .onstart = _aos_wifi_client_onstart,
        .onstop = _aos_wifi_client_onstop,
        .args = ctx};
    _task = aos_task_alloc(&task_config);

    if (!ctx ||
        !_task ||
        aos_task_handler_set(_task, _aos_wifi_client_connect_handler, AOS_WIFI_CLIENT_EVT_CONNECT) ||
        aos_task_handler_set(_task, _aos_wifi_client_disconnect_handler, AOS_WIFI_CLIENT_EVT_DISCONNECT) ||
        aos_task_handler_set(_task, _aos_wifi_client_scan_handler, AOS_WIFI_CLIENT_EVT_SCAN) ||
        aos_task_handler_set(_task, _aos_wifi_client_onconnected_handler, AOS_WIFI_CLIENT_EVT_CONNECTED) ||
        aos_task_handler_set(_task, _aos_wifi_client_ondisconnected_handler, AOS_WIFI_CLIENT_EVT_DISCONNECTED) ||
        aos_task_handler_set(_task, _aos_wifi_client_onscandone_handler, AOS_WIFI_CLIENT_EVT_SCANDONE))
        goto wifi_alloc_err;

    esp_event_loop_create_default(); // This is "sort of" idempotent. Calling again reaches same state, but returns different error.
    ctx->config = *config;

    return;

wifi_alloc_err:
    aos_task_free(_task);
    free(ctx);
    _task = NULL;
}

AOS_DEFINE(aos_wifi_client_start, unsigned int)
aos_future_t *aos_wifi_client_start(aos_future_t *future)
{
    return aos_task_start(_task, future);
}
static uint32_t _aos_wifi_client_onstart(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);
    AOS_ARGS_T(aos_wifi_client_start) *args = aos_args_get(future);

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_init_config.nvs_enable = 0;

    ctx->netif = esp_netif_create_default_wifi_sta();
    if (!ctx->netif)
    {
        args->out_err = 1;
        aos_resolve(future);
        return 1;
    }

    if (esp_wifi_init(&wifi_init_config) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK || // TODO: May improve performance at the expense of power consumption. Make this configurable.
        esp_wifi_start() != ESP_OK ||
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, _aos_wifi_client_event_handler, NULL, &ctx->ip_handler_instance) != ESP_OK ||
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, _aos_wifi_client_event_handler, NULL, &ctx->wifi_handler_instance) != ESP_OK)
    {
        args->out_err = 1;
        aos_resolve(future);
        return 1;
    }

    aos_resolve(future);
    return 0;
}

AOS_DEFINE(aos_wifi_client_stop)
aos_future_t *aos_wifi_client_stop(aos_future_t *future)
{
    return aos_task_stop(_task, future);
}
static uint32_t _aos_wifi_client_onstop(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);

    _aos_wifi_client_stopcurrentscan(task);
    _aos_wifi_client_disconnect(task);

    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, ctx->wifi_handler_instance);
    ctx->wifi_handler_instance = NULL;
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ctx->ip_handler_instance);
    ctx->ip_handler_instance = NULL;

    esp_wifi_stop();
    esp_wifi_deinit();

    esp_netif_destroy_default_wifi(ctx->netif);
    ctx->netif = NULL;

    aos_resolve(future);
    return 0;
}

AOS_DEFINE(aos_wifi_client_connect, const char *, const char *, unsigned int)
aos_future_t *aos_wifi_client_connect(aos_future_t *future)
{
    return aos_task_send(_task, AOS_WIFI_CLIENT_EVT_CONNECT, future);
}
static void _aos_wifi_client_connect_handler(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    AOS_ARGS_T(aos_wifi_client_connect) *args = aos_args_get(future);
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);

    switch (ctx->state)
    {
    case AOS_WIFI_CLIENT_STATE_DISCONNECTED:
    case AOS_WIFI_CLIENT_STATE_CONNECTED:
    case AOS_WIFI_CLIENT_STATE_CONNECTING:
    case AOS_WIFI_CLIENT_STATE_RECONNECTING:
    {
        // Input checking
        wifi_config_t config = {};
        if (strlen(args->in_ssid) > sizeof(config.sta.ssid) / sizeof(char) ||
            strlen(args->in_password) > sizeof(config.sta.password) / sizeof(char))
        {
            ESP_LOGW(_tag, "SSID or password too long (SSID_max:%u password_max:%u)", sizeof(config.sta.ssid) / sizeof(char), sizeof(config.sta.password) / sizeof(char));
            args->out_err = 1;
            aos_resolve(future);
            break;
        }

        // Get current configuration
        wifi_config_t old_config = {};
        esp_err_t err = esp_wifi_get_config(ESP_IF_WIFI_STA, &old_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(_tag, "Could not get current config (ESP_error:%s)", esp_err_to_name(err));
            _aos_wifi_client_disconnect(task);
            ctx->state = AOS_WIFI_CLIENT_STATE_DISCONNECTED;
            args->out_err = 1;
            aos_resolve(future);
            break;
        }

        // Do not reconnect if configuration did not change
        if (ctx->state == AOS_WIFI_CLIENT_STATE_CONNECTED &&
            !strncmp((char *)old_config.sta.ssid, args->in_ssid, sizeof(old_config.sta.ssid) / sizeof(char)) &&
            !strncmp((char *)old_config.sta.password, args->in_password, sizeof(old_config.sta.password) / sizeof(char)))
        {
            ESP_LOGI(_tag, "Already connected to specified network (ssid:%s)", args->in_ssid);
            args->out_err = 0;
            aos_resolve(future);
            break;
        }

        // Disconnect in case we are connected
        _aos_wifi_client_disconnect(task);

        // Prepare config
        strcpy((char *)config.sta.ssid, args->in_ssid);
        strcpy((char *)config.sta.password, args->in_password);
        err = esp_wifi_set_config(ESP_IF_WIFI_STA, &config);
        if (err != ESP_OK)
        {
            ESP_LOGE(_tag, "Could not set config (ESP_error:%s)", esp_err_to_name(err));
            _aos_wifi_client_disconnect(task);
            ctx->state = AOS_WIFI_CLIENT_STATE_DISCONNECTED;
            args->out_err = 1;
            aos_resolve(future);
            break;
        }

        // Reset state and try to connect
        ctx->connection_attempt = 0;
        ctx->reconnection_attempt = 0;
        err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            ESP_LOGE(_tag, "Could not start connection (%s)", esp_err_to_name(err));
            _aos_wifi_client_disconnect(task);
            ctx->state = AOS_WIFI_CLIENT_STATE_DISCONNECTED;
            args->out_err = 1;
            aos_resolve(future);
            break;
        }
        ctx->connect_future = future;
        ctx->state = AOS_WIFI_CLIENT_STATE_CONNECTING;
        break;
    }
    }
}

AOS_DEFINE(aos_wifi_client_disconnect)
aos_future_t *aos_wifi_client_disconnect(aos_future_t *future)
{
    return aos_task_send(_task, AOS_WIFI_CLIENT_EVT_DISCONNECT, future);
}
static void _aos_wifi_client_disconnect_handler(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);

    switch (ctx->state)
    {
    case AOS_WIFI_CLIENT_STATE_DISCONNECTED:
    case AOS_WIFI_CLIENT_STATE_CONNECTED:
    case AOS_WIFI_CLIENT_STATE_CONNECTING:
    case AOS_WIFI_CLIENT_STATE_RECONNECTING:
    {
        _aos_wifi_client_disconnect(task);
        ESP_LOGI(_tag, "Disconnected");
        ctx->state = AOS_WIFI_CLIENT_STATE_DISCONNECTED;
        aos_resolve(future);
        break;
    }
    }
}

// TODO: Do we have to deal with out-of-sync notifications in case we connect->disconnect->connect quickly in succession? When should we expect them? IDF is not clear.
AOS_DECLARE(_aos_wifi_client_onconnected, esp_netif_ip_info_t *ip_info)
AOS_DEFINE(_aos_wifi_client_onconnected, esp_netif_ip_info_t *)
static void _aos_wifi_client_onconnected_handler(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    ESP_LOGI(_tag, "Connected");
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);
    AOS_ARGS_T(_aos_wifi_client_onconnected) *args = aos_args_get(future);

    switch (ctx->state)
    {
    case AOS_WIFI_CLIENT_STATE_CONNECTING:
    case AOS_WIFI_CLIENT_STATE_RECONNECTING:
    case AOS_WIFI_CLIENT_STATE_CONNECTED:
    {
        // Resolve connect future if any
        if (ctx->connect_future)
        {
            AOS_ARGS_T(aos_wifi_client_connect) *connect_future_args = aos_args_get(ctx->connect_future);
            connect_future_args->out_err = 0;
            aos_resolve(ctx->connect_future);
            ctx->connect_future = NULL;
        }

        // Reset reconnection counter
        ctx->reconnection_attempt = 0;

        // Store ip information
        ctx->ip_info = args->ip_info; // TODO: Shall we copy them, free them, or just a pointer is fine?

        // If we are reconnecting, raise event
        if (ctx->state == AOS_WIFI_CLIENT_STATE_RECONNECTING)
        {
            ctx->config.event_handler(AOS_WIFI_CLIENT_EVENT_RECONNECTED, NULL);
        }

        // Set state
        ctx->state = AOS_WIFI_CLIENT_STATE_CONNECTED;

        aos_resolve(future);
        break;
    }
    case AOS_WIFI_CLIENT_STATE_DISCONNECTED:
    {
        // Target state is DISCONNECTED, thus do nothing. It is likely a late notification.
        aos_resolve(future);
        break;
    }
    }
}

AOS_DECLARE(_aos_wifi_client_ondisconnected, wifi_event_sta_disconnected_t *event)
AOS_DEFINE(_aos_wifi_client_ondisconnected, wifi_event_sta_disconnected_t *)
static void _aos_wifi_client_ondisconnected_handler(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);
    // AOS_ARGS_T(_aos_wifi_client_ondisconnected) *args = aos_args_get(future);

    switch (ctx->state)
    {
    case AOS_WIFI_CLIENT_STATE_CONNECTED:
    case AOS_WIFI_CLIENT_STATE_RECONNECTING:
    case AOS_WIFI_CLIENT_STATE_CONNECTING:
    {
        // Are we connecting?
        if (ctx->connect_future)
        {
            // If we tried too many times, just disconnect
            if (ctx->connection_attempt > ctx->config.connection_attempts)
            {
                ESP_LOGE(_tag, "Maximum connection attempts reached, disconnecting (%u)", ctx->config.connection_attempts);
                _aos_wifi_client_disconnect(task);
                ctx->state = AOS_WIFI_CLIENT_STATE_DISCONNECTED;
                aos_resolve(future);
                break;
            }
            // Else, try once more
            ctx->connection_attempt++;
            ESP_LOGI(_tag, "Attempting connection (attempt:%u)", ctx->connection_attempt);
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK)
            {
                ESP_LOGE(_tag, "Could not start connection (ESP_error:%s)", esp_err_to_name(err));
                _aos_wifi_client_disconnect(task);
                ctx->state = AOS_WIFI_CLIENT_STATE_DISCONNECTED;
                aos_resolve(future);
                break;
            }
            aos_resolve(future);
            break;
        }
        // No, we are recovering
        if (ctx->reconnection_attempt > ctx->config.reconnection_attempts)
        {
            ESP_LOGE(_tag, "Maximum reconnection attempts reached, disconnecting (%u)", ctx->config.reconnection_attempts);
            _aos_wifi_client_disconnect(task);
            ctx->state = AOS_WIFI_CLIENT_STATE_DISCONNECTED;
            ctx->config.event_handler(AOS_WIFI_CLIENT_EVENT_DISCONNECTED, NULL);
            aos_resolve(future);
            break;
        }
        ctx->reconnection_attempt++;
        ESP_LOGI(_tag, "Attempting reconnection (attempt:%u)", ctx->reconnection_attempt);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK)
        {
            ESP_LOGE(_tag, "Could not start connection (ESP_error:%s)", esp_err_to_name(err));
            _aos_wifi_client_disconnect(task);
            ctx->state = AOS_WIFI_CLIENT_STATE_DISCONNECTED;
            aos_resolve(future);
            break;
        }
        ctx->state = AOS_WIFI_CLIENT_STATE_RECONNECTING;
        ctx->config.event_handler(AOS_WIFI_CLIENT_EVENT_RECONNECTING, NULL);
        ESP_LOGI(_tag, "Connection recovered");
        aos_resolve(future);
        break;
    }
    case AOS_WIFI_CLIENT_STATE_DISCONNECTED:
    {
        // Target state is DISCONNECTED, thus do nothing. It is likely a late notification.
        aos_resolve(future);
        break;
    }
    }
}

AOS_DEFINE(aos_wifi_client_scan, aos_wifi_client_scan_result_t *, size_t, size_t, uint32_t)
aos_future_t *aos_wifi_client_scan(aos_future_t *future)
{
    return aos_task_send(_task, AOS_WIFI_CLIENT_EVT_SCAN, future);
}
static void _aos_wifi_client_scan_handler(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);
    AOS_ARGS_T(aos_wifi_client_scan) *args = aos_args_get(future);

    switch (ctx->state)
    {
    case AOS_WIFI_CLIENT_STATE_CONNECTED:
    case AOS_WIFI_CLIENT_STATE_DISCONNECTED:
    case AOS_WIFI_CLIENT_STATE_CONNECTING:
    case AOS_WIFI_CLIENT_STATE_RECONNECTING:
    {
        // Resolve unfinished scan if any
        _aos_wifi_client_stopcurrentscan(task);

        // Start scan
        esp_err_t err = esp_wifi_scan_start(NULL, false);
        if (err != ESP_OK)
        {
            // We cannot scan while connecting according to documentation
            ESP_LOGE(_tag, "Could not start scan (ESP_error:%s)", esp_err_to_name(err));
            args->out_err = 1;
            aos_resolve(future);
            break;
        }
        ESP_LOGI(_tag, "Scanning");
        ctx->scan_future = future;
        break;
    }
    }
}

AOS_DECLARE(_aos_wifi_client_onscandone)
AOS_DEFINE(_aos_wifi_client_onscandone)
static void _aos_wifi_client_onscandone_handler(aos_task_t *task, aos_future_t *future)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);

    switch (ctx->state)
    {
    case AOS_WIFI_CLIENT_STATE_CONNECTED:
    case AOS_WIFI_CLIENT_STATE_DISCONNECTED:
    case AOS_WIFI_CLIENT_STATE_CONNECTING:
    case AOS_WIFI_CLIENT_STATE_RECONNECTING:
    {
        if (!ctx->scan_future)
        {
            // We need to call this to free memory in the driver according to esp_wifi_scan_start docs
            uint16_t n = 0;
            wifi_ap_record_t m = {};
            esp_err_t err = esp_wifi_scan_get_ap_records(&n, &m);
            ESP_LOGW(_tag, "Could not find scan future, cleaning up (esp_wifi_scan_get_ap_records:%s)", esp_err_to_name(err));
            aos_resolve(future);
            break;
        }

        AOS_ARGS_T(aos_wifi_client_scan) *scan_args = aos_args_get(ctx->scan_future);
        wifi_ap_record_t *results = NULL;

        // Allocate temporary structure for results
        uint16_t results_cnt = scan_args->in_results_size;
        results = calloc(results_cnt, sizeof(wifi_ap_record_t));
        if (!results)
        {
            ESP_LOGE(_tag, "Allocation error");
            scan_args->out_err = 1; // TODO: Ensure correct error
            goto _aos_wifi_client_onscandone_handler_end;
        }

        // Get results
        esp_err_t err = esp_wifi_scan_get_ap_records(&results_cnt, results);
        if (err != ESP_OK)
        {
            ESP_LOGE(_tag, "Could not get AP records (esp_wifi_scan_get_ap_records:%s)", esp_err_to_name(err));
            scan_args->out_err = 2; // TODO: Ensure correct error
            goto _aos_wifi_client_onscandone_handler_end;
        }
        for (size_t i = 0; i < results_cnt; i++)
        {
            memset(scan_args->in_results[i].ssid, 0, sizeof(scan_args->in_results[i].ssid));
            strncpy(scan_args->in_results[i].ssid, (char *)results[i].ssid, sizeof(results[i].ssid) / sizeof(char));
            scan_args->in_results[i].open = results[i].authmode == WIFI_AUTH_OPEN ? 1 : 0; // TODO: Likely we want something more elaborate here
            scan_args->in_results[i].strength = ((float)results[i].rssi / INT8_MAX) + 1;   // TODO: Assess this is the correct scale, RSSI scale depends on manufacturer and no docs could be found in IDF
        }
        scan_args->out_results_count = results_cnt;
        scan_args->out_err = 0;
        ESP_LOGI(_tag, "Scan done");

    _aos_wifi_client_onscandone_handler_end:
        free(results);
        aos_resolve(future);
        aos_resolve(ctx->scan_future);
        ctx->scan_future = NULL;
        break;
    }
    }
}

static void _aos_wifi_client_disconnect(aos_task_t *task)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);
    esp_wifi_disconnect();
    if (ctx->connect_future)
    {
        AOS_ARGS_T(aos_wifi_client_connect) *args = aos_args_get(ctx->connect_future);
        args->out_err = 1;
        aos_resolve(ctx->connect_future);
        ctx->connect_future = NULL;
    }
}

static void _aos_wifi_client_stopcurrentscan(aos_task_t *task)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    _aos_wifi_client_ctx_t *ctx = aos_task_args_get(task);
    if (ctx->scan_future)
    {
        esp_err_t err0 = esp_wifi_scan_stop();
        // Cleanup incomplete scan results
        uint16_t n = 0;
        wifi_ap_record_t m = {};
        esp_err_t err1 = esp_wifi_scan_get_ap_records(&n, &m);
        ESP_LOGD(_tag, "Stopped scan (esp_wifi_scan_stop:%s esp_wifi_scan_get_ap_records:%s)", esp_err_to_name(err0), esp_err_to_name(err1));
        AOS_ARGS_T(aos_wifi_client_scan) *scan_future_args = aos_args_get(ctx->scan_future);
        scan_future_args->out_err = 1;
        aos_resolve(ctx->scan_future);
        ctx->scan_future = NULL;
    }
}

static void _aos_wifi_client_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGD(_tag, "%s", __FUNCTION__);
    ESP_LOGD(_tag, "event_base:%s event_id:%d", event_base, event_id);
    if (event_base == WIFI_EVENT)
    {
        if (event_id == WIFI_EVENT_STA_DISCONNECTED)
        {
            wifi_event_sta_disconnected_t *event = event_data; // FIXME: Is this freed on handler exit? Should we duplicate it?
            aos_future_t *future = AOS_FORGETTABLE_ALLOC_T(_aos_wifi_client_ondisconnected)(event);
            if (!future)
            {
                ESP_LOGE(_tag, "Allocation error");
                return;
            }
            aos_task_send(_task, AOS_WIFI_CLIENT_EVT_DISCONNECTED, future);
        }
        else if (event_id == WIFI_EVENT_SCAN_DONE)
        {
            aos_future_t *future = AOS_FORGETTABLE_ALLOC_T(_aos_wifi_client_onscandone)();
            if (!future)
            {
                ESP_LOGE(_tag, "Allocation error");
                return;
            }
            aos_task_send(_task, AOS_WIFI_CLIENT_EVT_SCANDONE, future);
        }
    }
    else if (event_base == IP_EVENT)
    {
        if (event_id == IP_EVENT_STA_GOT_IP)
        {
            aos_future_t *future = AOS_FORGETTABLE_ALLOC_T(_aos_wifi_client_onconnected)(&((ip_event_got_ip_t *)event_data)->ip_info); // FIXME: Is this freed on handler exit? Should we duplicate it?
            if (!future)
            {
                ESP_LOGE(_tag, "Allocation error");
                return;
            }
            aos_task_send(_task, AOS_WIFI_CLIENT_EVT_CONNECTED, future);
        }
    }
}

/**
 * WiFi driver disconnection reason reference:
 *  WIFI_REASON_UNSPECIFIED              = 1,
 *  WIFI_REASON_AUTH_EXPIRE              = 2,
 *  WIFI_REASON_AUTH_LEAVE               = 3,
 *  WIFI_REASON_ASSOC_EXPIRE             = 4,
 *  WIFI_REASON_ASSOC_TOOMANY            = 5,
 *  WIFI_REASON_NOT_AUTHED               = 6,
 *  WIFI_REASON_NOT_ASSOCED              = 7,
 *  WIFI_REASON_ASSOC_LEAVE              = 8,
 *  WIFI_REASON_ASSOC_NOT_AUTHED         = 9,
 *  WIFI_REASON_DISASSOC_PWRCAP_BAD      = 10,
 *  WIFI_REASON_DISASSOC_SUPCHAN_BAD     = 11,
 *  WIFI_REASON_IE_INVALID               = 13,
 *  WIFI_REASON_MIC_FAILURE              = 14,
 *  WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT   = 15,
 *  WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT = 16,
 *  WIFI_REASON_IE_IN_4WAY_DIFFERS       = 17,
 *  WIFI_REASON_GROUP_CIPHER_INVALID     = 18,
 *  WIFI_REASON_PAIRWISE_CIPHER_INVALID  = 19,
 *  WIFI_REASON_AKMP_INVALID             = 20,
 *  WIFI_REASON_UNSUPP_RSN_IE_VERSION    = 21,
 *  WIFI_REASON_INVALID_RSN_IE_CAP       = 22,
 *  WIFI_REASON_802_1X_AUTH_FAILED       = 23,
 *  WIFI_REASON_CIPHER_SUITE_REJECTED    = 24,
 *  WIFI_REASON_INVALID_PMKID            = 53,
 *  WIFI_REASON_BEACON_TIMEOUT           = 200,
 *  WIFI_REASON_NO_AP_FOUND              = 201,
 *  WIFI_REASON_AUTH_FAIL                = 202,
 *  WIFI_REASON_ASSOC_FAIL               = 203,
 *  WIFI_REASON_HANDSHAKE_TIMEOUT        = 204,
 *  WIFI_REASON_CONNECTION_FAIL          = 205,
 */
