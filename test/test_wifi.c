#include <aos_wifi_client.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <test_macros.h>
#include <unity.h>
#include <unity_test_runner.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static bool _isinit = false;
static const char *_test_ssid = "MY_SSID";
static const char *_test_password = "MY_PASSWORD";

static void test_event_handler(aos_wifi_client_event_t event, void *args)
{
    printf("Event:%d\n", event);
}

static void test_init()
{
    if (!_isinit)
    {
        ESP_ERROR_CHECK(esp_netif_init());
    }
    _isinit = true;

    aos_wifi_client_config_t config = {
        .connection_attempts = UINT32_MAX,
        .reconnection_attempts = UINT32_MAX,
        .event_handler = test_event_handler};
    aos_wifi_client_init(&config);
}

TEST_CASE("Init", "[wifi_client]")
{
    test_init();
}

TEST_CASE("Start/stop", "[wifi_client]")
{
    test_init();
    TEST_HEAP_START

    aos_future_t *start = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_start)(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_start(start))));
    AOS_ARGS_T(aos_wifi_client_start) *args = aos_args_get(start);
    TEST_ASSERT_EQUAL(0, args->out_err);
    aos_awaitable_free(start);

    aos_future_t *stop = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_stop)();
    TEST_ASSERT_NOT_NULL(stop);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_stop(stop))));
    aos_awaitable_free(stop);

    vTaskDelay(pdMS_TO_TICKS(1));

    TEST_HEAP_STOP
}

TEST_CASE("Start/connect/disconnect/stop", "[wifi_client]")
{
    test_init();
    TEST_HEAP_START

    aos_future_t *start = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_start)(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_start(start))));
    AOS_ARGS_T(aos_wifi_client_start) *start_args = aos_args_get(start);
    TEST_ASSERT_EQUAL(0, start_args->out_err);
    aos_awaitable_free(start);

    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_connect)(_test_ssid, _test_password, 0);
    TEST_ASSERT_NOT_NULL(connect);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_connect(connect))));
    AOS_ARGS_T(aos_wifi_client_connect) *connect_args = aos_args_get(connect);
    TEST_ASSERT_EQUAL(0, connect_args->out_err);
    aos_awaitable_free(connect);

    aos_future_t *disconnect = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_disconnect)();
    TEST_ASSERT_NOT_NULL(disconnect);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_disconnect(disconnect))));
    aos_awaitable_free(disconnect);

    aos_future_t *stop = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_stop)();
    TEST_ASSERT_NOT_NULL(stop);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_stop(stop))));
    aos_awaitable_free(stop);

    vTaskDelay(pdMS_TO_TICKS(1));

    TEST_HEAP_STOP
}

TEST_CASE("Start/connect/connect/disconnect/connect/stop (late await)", "[wifi_client]")
{
    test_init();
    TEST_HEAP_START

    aos_future_t *start = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_start)(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_start(start))));
    AOS_ARGS_T(aos_wifi_client_start) *start_args = aos_args_get(start);
    TEST_ASSERT_EQUAL(0, start_args->out_err);
    aos_awaitable_free(start);

    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_connect)(_test_ssid, _test_password, 0);
    TEST_ASSERT_NOT_NULL(connect);
    aos_wifi_client_connect(connect);

    aos_future_t *connect1 = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_connect)(_test_ssid, _test_password, 0);
    TEST_ASSERT_NOT_NULL(connect1);
    aos_wifi_client_connect(connect1);

    aos_future_t *disconnect = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_disconnect)();
    TEST_ASSERT_NOT_NULL(disconnect);
    aos_wifi_client_disconnect(disconnect);

    aos_future_t *connect2 = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_connect)(_test_ssid, _test_password, 0);
    TEST_ASSERT_NOT_NULL(connect2);
    aos_wifi_client_connect(connect2);

    aos_future_t *stop = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_stop)();
    TEST_ASSERT_NOT_NULL(stop);
    aos_wifi_client_stop(stop);

    printf("Awaiting 1\n");
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(connect)));
    AOS_ARGS_T(aos_wifi_client_connect) *connect_args = aos_args_get(connect);
    TEST_ASSERT_EQUAL(1, connect_args->out_err);
    aos_awaitable_free(connect);
    printf("Awaiting 2\n");
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(connect1)));
    AOS_ARGS_T(aos_wifi_client_connect) *connect1_args = aos_args_get(connect1);
    TEST_ASSERT_EQUAL(1, connect1_args->out_err);
    aos_awaitable_free(connect1);
    printf("Awaiting 3\n");
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(disconnect)));
    aos_awaitable_free(disconnect);
    printf("Awaiting 4\n");
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(connect2)));
    AOS_ARGS_T(aos_wifi_client_connect) *connect2_args = aos_args_get(connect2);
    TEST_ASSERT_EQUAL(1, connect2_args->out_err);
    aos_awaitable_free(connect2);
    printf("Awaiting 5\n");
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(stop)));
    aos_awaitable_free(stop);

    vTaskDelay(pdMS_TO_TICKS(1));

    TEST_HEAP_STOP
}

TEST_CASE("Start/scan/stop", "[wifi_client]")
{
    test_init();
    TEST_HEAP_START

    aos_future_t *start = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_start)(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_start(start))));
    AOS_ARGS_T(aos_wifi_client_start) *start_args = aos_args_get(start);
    TEST_ASSERT_EQUAL(0, start_args->out_err);
    aos_awaitable_free(start);

    aos_wifi_client_scan_result_t results[10] = {};
    aos_future_t *scan = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_scan)(results, 10, 0, 0);
    TEST_ASSERT_NOT_NULL(scan);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_scan(scan))));
    AOS_ARGS_T(aos_wifi_client_scan) *scan_args = aos_args_get(scan);
    TEST_ASSERT_EQUAL(0, scan_args->out_err);
    for (size_t i = 0; i < scan_args->out_results_count; i++)
    {
        printf("Scan result (ssid:%s, strength:%f, open:%u)\n", results[i].ssid, results[i].strength, results[i].open);
    }
    aos_awaitable_free(scan);

    aos_future_t *stop = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_stop)();
    TEST_ASSERT_NOT_NULL(stop);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_stop(stop))));
    aos_awaitable_free(stop);

    vTaskDelay(pdMS_TO_TICKS(1));

    TEST_HEAP_STOP
}

TEST_CASE("Start/scan/stop (late await)", "[wifi_client]")
{
    test_init();
    TEST_HEAP_START

    aos_future_t *start = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_start)(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_start(start))));
    AOS_ARGS_T(aos_wifi_client_start) *start_args = aos_args_get(start);
    TEST_ASSERT_EQUAL(0, start_args->out_err);
    aos_awaitable_free(start);

    aos_wifi_client_scan_result_t results[10] = {};
    aos_future_t *scan = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_scan)(results, 10, 0, 0);
    TEST_ASSERT_NOT_NULL(scan);
    aos_wifi_client_scan(scan);

    aos_future_t *stop = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_stop)();
    TEST_ASSERT_NOT_NULL(stop);
    aos_wifi_client_stop(stop);

    TEST_ASSERT_TRUE(aos_isresolved(aos_await(scan)));
    AOS_ARGS_T(aos_wifi_client_scan) *scan_args = aos_args_get(scan);
    TEST_ASSERT_EQUAL(1, scan_args->out_err);
    aos_awaitable_free(scan);

    TEST_ASSERT_TRUE(aos_isresolved(aos_await(stop)));
    aos_awaitable_free(stop);

    vTaskDelay(pdMS_TO_TICKS(1));

    TEST_HEAP_STOP
}

TEST_CASE("Start/connect/scan/stop (late await)", "[wifi_client]")
{
    test_init();
    TEST_HEAP_START

    aos_future_t *start = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_start)(0);
    TEST_ASSERT_NOT_NULL(start);
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(aos_wifi_client_start(start))));
    AOS_ARGS_T(aos_wifi_client_start) *start_args = aos_args_get(start);
    TEST_ASSERT_EQUAL(0, start_args->out_err);
    aos_awaitable_free(start);

    aos_future_t *connect = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_connect)(_test_ssid, _test_password, 0);
    TEST_ASSERT_NOT_NULL(connect);
    aos_wifi_client_connect(connect);

    aos_wifi_client_scan_result_t results[10] = {};
    aos_future_t *scan = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_scan)(results, 10, 0, 0);
    TEST_ASSERT_NOT_NULL(scan);
    aos_wifi_client_scan(scan);

    aos_future_t *stop = AOS_AWAITABLE_ALLOC_T(aos_wifi_client_stop)();
    TEST_ASSERT_NOT_NULL(stop);
    aos_wifi_client_stop(stop);

    printf("Awaiting 1\n");
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(connect)));
    AOS_ARGS_T(aos_wifi_client_connect) *connect_args = aos_args_get(connect);
    TEST_ASSERT_EQUAL(1, connect_args->out_err);
    aos_awaitable_free(connect);
    printf("Awaiting 2\n");
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(scan)));
    AOS_ARGS_T(aos_wifi_client_scan) *scan_args = aos_args_get(scan);
    TEST_ASSERT_EQUAL(1, scan_args->out_err);
    aos_awaitable_free(scan);
    printf("Awaiting 3\n");
    TEST_ASSERT_TRUE(aos_isresolved(aos_await(stop)));
    aos_awaitable_free(stop);

    vTaskDelay(pdMS_TO_TICKS(1));

    TEST_HEAP_STOP
}