#include "web_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#define WEB_COMMAND_BUFFER_SIZE 768U
#define WEB_AUTH_BUFFER_SIZE 256U
#define WEB_SESSION_TOKEN_SIZE 33U
#define WEB_SESSION_TTL_US (30LL * 60LL * 1000000LL)

static const char *TAG = "web_console";
static WebConsoleTextHandler s_status_handler;
static WebConsoleTextHandler s_events_handler;
static WebConsoleTextHandler s_audit_handler;
static WebConsoleCommandHandler s_command_handler;
static WebConsoleAuthHandler s_auth_handler;
static char s_session_token[WEB_SESSION_TOKEN_SIZE];
static int64_t s_session_expiry_us;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t servercert_pem_start[] asm("_binary_servercert_pem_start");
extern const uint8_t servercert_pem_end[] asm("_binary_servercert_pem_end");
extern const uint8_t serverkey_pem_start[] asm("_binary_serverkey_pem_start");
extern const uint8_t serverkey_pem_end[] asm("_binary_serverkey_pem_end");

static esp_err_t SendText(httpd_req_t *request,
                          const char *content_type,
                          const char *body)
{
  httpd_resp_set_type(request, content_type);
  return httpd_resp_send(request, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t SendBuiltText(httpd_req_t *request,
                               const char *content_type,
                               WebConsoleTextHandler handler)
{
  char body[2048];

  if (handler == NULL)
  {
    return httpd_resp_send_err(request,
                               HTTPD_500_INTERNAL_SERVER_ERROR,
                               "handler unavailable");
  }

  body[0] = '\0';
  handler(body, sizeof(body));
  return SendText(request, content_type, body);
}

static esp_err_t SendUnauthorized(httpd_req_t *request)
{
  httpd_resp_set_status(request, "401 Unauthorized");
  return SendText(request, "application/json", "{\"authenticated\":false}");
}

static void WebClearSession(void)
{
  s_session_token[0] = '\0';
  s_session_expiry_us = 0;
}

static void WebCreateSession(void)
{
  uint32_t random[4];

  for (size_t index = 0U; index < (sizeof(random) / sizeof(random[0])); index++)
  {
    random[index] = esp_random();
  }

  snprintf(s_session_token,
           sizeof(s_session_token),
           "%08lX%08lX%08lX%08lX",
           (unsigned long)random[0],
           (unsigned long)random[1],
           (unsigned long)random[2],
           (unsigned long)random[3]);
  s_session_expiry_us = esp_timer_get_time() + WEB_SESSION_TTL_US;
}

static bool WebSessionValid(httpd_req_t *request)
{
  char cookie[256];
  char expected[48];
  size_t cookie_length;

  if ((s_session_token[0] == '\0') ||
      (esp_timer_get_time() >= s_session_expiry_us))
  {
    WebClearSession();
    return false;
  }

  cookie_length = httpd_req_get_hdr_value_len(request, "Cookie");
  if ((cookie_length == 0U) || (cookie_length >= sizeof(cookie)))
  {
    return false;
  }
  if (httpd_req_get_hdr_value_str(request,
                                  "Cookie",
                                  cookie,
                                  sizeof(cookie)) != ESP_OK)
  {
    return false;
  }

  snprintf(expected, sizeof(expected), "session=%s", s_session_token);
  return strstr(cookie, expected) != NULL;
}

static bool WebReadJsonBody(httpd_req_t *request,
                            char *buffer,
                            size_t buffer_size)
{
  int received = 0;

  if ((buffer == NULL) || (buffer_size < 2U) ||
      (request->content_len <= 0) ||
      (request->content_len >= (int)buffer_size))
  {
    return false;
  }

  while (received < request->content_len)
  {
    int result = httpd_req_recv(request,
                                &buffer[received],
                                request->content_len - received);
    if (result <= 0)
    {
      return false;
    }
    received += result;
  }
  buffer[received] = '\0';
  return true;
}

static esp_err_t IndexHandler(httpd_req_t *request)
{
  size_t length = (size_t)(index_html_end - index_html_start);

  httpd_resp_set_type(request, "text/html; charset=utf-8");
  return httpd_resp_send(request, (const char *)index_html_start, length);
}

static esp_err_t StatusHandler(httpd_req_t *request)
{
  if (!WebSessionValid(request))
  {
    return SendUnauthorized(request);
  }
  return SendBuiltText(request, "application/json", s_status_handler);
}

static esp_err_t EventsHandler(httpd_req_t *request)
{
  if (!WebSessionValid(request))
  {
    return SendUnauthorized(request);
  }
  return SendBuiltText(request, "application/json", s_events_handler);
}

static esp_err_t AuditHandler(httpd_req_t *request)
{
  if (!WebSessionValid(request))
  {
    return SendUnauthorized(request);
  }
  return SendBuiltText(request, "application/json", s_audit_handler);
}

static esp_err_t CommandHandler(httpd_req_t *request)
{
  char payload[WEB_COMMAND_BUFFER_SIZE];

  if (!WebSessionValid(request))
  {
    return SendUnauthorized(request);
  }

  if (!WebReadJsonBody(request, payload, sizeof(payload)))
  {
    return httpd_resp_send_err(request,
                               HTTPD_400_BAD_REQUEST,
                               "invalid command size");
  }

  if ((s_command_handler == NULL) ||
      !s_command_handler(payload, (int)strlen(payload)))
  {
    return httpd_resp_send_err(request,
                               HTTPD_400_BAD_REQUEST,
                               "command rejected");
  }

  return SendText(request, "application/json", "{\"submitted\":true}");
}

static esp_err_t LoginHandler(httpd_req_t *request)
{
  char payload[WEB_AUTH_BUFFER_SIZE];
  cJSON *root;
  const cJSON *username;
  const cJSON *password;
  char cookie[160];

  if (!WebReadJsonBody(request, payload, sizeof(payload)))
  {
    return httpd_resp_send_err(request,
                               HTTPD_400_BAD_REQUEST,
                               "invalid login request");
  }

  root = cJSON_ParseWithLength(payload, strlen(payload));
  if (root == NULL)
  {
    return httpd_resp_send_err(request,
                               HTTPD_400_BAD_REQUEST,
                               "invalid login JSON");
  }

  username = cJSON_GetObjectItemCaseSensitive(root, "username");
  password = cJSON_GetObjectItemCaseSensitive(root, "password");
  if (!cJSON_IsString(username) || (username->valuestring == NULL) ||
      !cJSON_IsString(password) || (password->valuestring == NULL) ||
      (s_auth_handler == NULL) ||
      !s_auth_handler(username->valuestring, password->valuestring))
  {
    cJSON_Delete(root);
    httpd_resp_set_status(request, "401 Unauthorized");
    return SendText(request, "application/json", "{\"authenticated\":false}");
  }
  cJSON_Delete(root);

  WebCreateSession();
  snprintf(cookie,
           sizeof(cookie),
           "session=%s; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=1800",
           s_session_token);
  httpd_resp_set_hdr(request, "Set-Cookie", cookie);
  return SendText(request, "application/json", "{\"authenticated\":true}");
}

static esp_err_t LogoutHandler(httpd_req_t *request)
{
  WebClearSession();
  httpd_resp_set_hdr(request,
                     "Set-Cookie",
                     "session=; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age=0");
  return SendText(request, "application/json", "{\"authenticated\":false}");
}

static esp_err_t FaviconHandler(httpd_req_t *request)
{
  httpd_resp_set_status(request, "204 No Content");
  return httpd_resp_send(request, NULL, 0);
}

bool WebConsole_Start(WebConsoleTextHandler status_handler,
                      WebConsoleTextHandler events_handler,
                      WebConsoleTextHandler audit_handler,
                      WebConsoleCommandHandler command_handler,
                      WebConsoleAuthHandler auth_handler)
{
  httpd_handle_t server = NULL;
  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = IndexHandler,
      .user_ctx = NULL,
  };
  httpd_uri_t index_html_uri = {
      .uri = "/index.html",
      .method = HTTP_GET,
      .handler = IndexHandler,
      .user_ctx = NULL,
  };
  httpd_uri_t status_uri = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = StatusHandler,
      .user_ctx = NULL,
  };
  httpd_uri_t events_uri = {
      .uri = "/api/events",
      .method = HTTP_GET,
      .handler = EventsHandler,
      .user_ctx = NULL,
  };
  httpd_uri_t audit_uri = {
      .uri = "/api/audit",
      .method = HTTP_GET,
      .handler = AuditHandler,
      .user_ctx = NULL,
  };
  httpd_uri_t command_uri = {
      .uri = "/api/command",
      .method = HTTP_POST,
      .handler = CommandHandler,
      .user_ctx = NULL,
  };
  httpd_uri_t favicon_uri = {
      .uri = "/favicon.ico",
      .method = HTTP_GET,
      .handler = FaviconHandler,
      .user_ctx = NULL,
  };
  httpd_uri_t login_uri = {
      .uri = "/api/login",
      .method = HTTP_POST,
      .handler = LoginHandler,
      .user_ctx = NULL,
  };
  httpd_uri_t logout_uri = {
      .uri = "/api/logout",
      .method = HTTP_POST,
      .handler = LogoutHandler,
      .user_ctx = NULL,
  };

  s_status_handler = status_handler;
  s_events_handler = events_handler;
  s_audit_handler = audit_handler;
  s_command_handler = command_handler;
  s_auth_handler = auth_handler;

  config.servercert = servercert_pem_start;
  config.servercert_len = (size_t)(servercert_pem_end - servercert_pem_start);
  config.prvtkey_pem = serverkey_pem_start;
  config.prvtkey_len = (size_t)(serverkey_pem_end - serverkey_pem_start);
  config.httpd.max_uri_handlers = 11U;
  config.port_secure = 443U;
  if (httpd_ssl_start(&server, &config) != ESP_OK)
  {
    ESP_LOGE(TAG, "HTTPS server start failed");
    return false;
  }

  if ((httpd_register_uri_handler(server, &index_uri) != ESP_OK) ||
      (httpd_register_uri_handler(server, &index_html_uri) != ESP_OK) ||
      (httpd_register_uri_handler(server, &status_uri) != ESP_OK) ||
      (httpd_register_uri_handler(server, &events_uri) != ESP_OK) ||
      (httpd_register_uri_handler(server, &audit_uri) != ESP_OK) ||
      (httpd_register_uri_handler(server, &command_uri) != ESP_OK) ||
      (httpd_register_uri_handler(server, &favicon_uri) != ESP_OK) ||
      (httpd_register_uri_handler(server, &login_uri) != ESP_OK) ||
      (httpd_register_uri_handler(server, &logout_uri) != ESP_OK))
  {
    ESP_LOGE(TAG, "HTTPS handler registration failed");
    (void)httpd_ssl_stop(server);
    return false;
  }

  ESP_LOGI(TAG, "HTTPS console started on port 443");
  return true;
}
