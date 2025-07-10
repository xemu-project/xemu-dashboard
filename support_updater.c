#include <json/json.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <windows.h>

#include <llhttp.h>

#include "main.h"

#define PORT             "443"
#define GITHUB_REPO_URL  "/repos/xemu-project/xemu-dashboard/releases/latest"
#define WANTED_FILE_NAME "default.xbe"

// https://docs.github.com/en/rest/using-the-rest-api/getting-started-with-the-rest-api?apiVersion=2022-11-28#user-agent
#define USER_AGENT "xemu-dashboard"

static mbedtls_net_context server_fd;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_ssl_context ssl;
static mbedtls_ssl_config conf;
static mbedtls_x509_crt cacert;
static int is_initialized = 0;

const unsigned char cert[] = {
#embed "assets/cacert.pem" suffix(, ) // No suffix
    0                                 // always null-terminated
};

static void mbedtls_debug(void *ctx, int level,
                          const char *file, int line,
                          const char *str)
{
    (void)ctx;
    (void)level;
    printf("%s:%d: %s\n", file, line, str);
}

typedef struct parser_data
{
    char *body_pointer;
    unsigned int body_length;
} parser_data_t;

static int handle_on_body_complete(llhttp_t *parser, const char *at, size_t length)
{
    parser_data_t *data = (parser_data_t *)parser->data;
    data->body_length = length;
    data->body_pointer = (char *)at;
    return 0;
}

static int do_request(mbedtls_net_context *server_fd, mbedtls_ssl_context *ssl, const char *host,
                      const char *request, char **header_out, char **content_out, int *content_length_out)
{
    int ret;

    mbedtls_ssl_session_reset(ssl);

    if ((ret = mbedtls_ssl_set_hostname(ssl, host)) != 0) {
        printf("Set hostname failed: -0x%x\n", -ret);
        return -1;
    }

    if ((ret = mbedtls_net_connect(server_fd, host, PORT, MBEDTLS_NET_PROTO_TCP)) != 0) {
        printf("Failed net_connect: -0x%x\n", -ret);
        return -1;
    }
    printf("Socket connected to %s:%s\n", host, PORT);

    while ((ret = mbedtls_ssl_handshake(ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            printf("Handshake failed: -0x%x\n", -ret);
            mbedtls_net_free(server_fd);
            return -1;
        }
    }

    // Write the request to the server
    if ((ret = mbedtls_ssl_write(ssl, (const unsigned char *)request, strlen(request))) <= 0) {
        printf("mbedtls_ssl_write failed: -0x%x\n", -ret);
        mbedtls_net_free(server_fd);
        return -1;
    }

    const int chunk_size = 32768;
    int bytes_read = 0;
    int buffer_size = chunk_size * 2;
    char *response_buffer = malloc(buffer_size);
    while ((ret = mbedtls_ssl_read(ssl, (unsigned char *)response_buffer + bytes_read, chunk_size)) > 0) {
        bytes_read += ret;

        // If the next read could overflow the buffer, we need to resize it
        if ((bytes_read + chunk_size) > buffer_size) {
            buffer_size += chunk_size;
            void *p = realloc(response_buffer, buffer_size);
            if (p == NULL) {
                printf("Memory allocation failed while reading response.\n");
                free(response_buffer);
                mbedtls_net_free(server_fd);
                return -1;
            } else {
                response_buffer = p;
            }
        }
    }

    mbedtls_ssl_close_notify(ssl);
    mbedtls_net_free(server_fd);

    // Shrink the buffer to the actual size read
    if (buffer_size > bytes_read) {
        void *p = realloc(response_buffer, bytes_read + 1);
        if (p != NULL) {
            response_buffer = p;
        }
    }

    // Execute the HTTP parser on the response buffer
    llhttp_t parser = {0};
    llhttp_settings_t settings = {0};
    llhttp_settings_init(&settings);
    llhttp_init(&parser, HTTP_BOTH, &settings);

    parser_data_t parser_data = {0};
    parser.data = &parser_data;
    settings.on_body = handle_on_body_complete;
    enum llhttp_errno err = llhttp_execute(&parser, response_buffer, bytes_read);
    if (err != HPE_OK && err != HPE_CB_MESSAGE_COMPLETE) {
        printf("llhttp_execute failed: %s\n", llhttp_get_error_reason(&parser));
        free(response_buffer);
        return -1;
    }

    if (header_out) {
        // Pull out the header from the response buffer
        char *header_end = strstr(response_buffer, "\r\n\r\n");
        assert(header_end != NULL);
        *header_end = '\0'; // Null-terminate the header
        *header_out = response_buffer;
    }
    if (content_out && parser_data.body_length > 0) {
        parser_data.body_pointer[parser_data.body_length] = '\0'; // Null terminate the content
        *content_out = parser_data.body_pointer;
    }
    if (content_length_out) {
        *content_length_out = parser_data.body_length;
    }

    return 0;
}

static void free_request(char **header, char **content)
{
    (void)content; // Content buffer is part of the header allocation
    free(*header);
    *header = NULL;
    *content = NULL;
}

int downloader_init(void)
{
    if (is_initialized) {
        return 0;
    }
    mbedtls_debug_set_threshold(0);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
    mbedtls_ssl_conf_dbg(&conf, mbedtls_debug, stdout);
    mbedtls_net_init(&server_fd);
    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    int ret = 0;

    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, NULL, 0)) != 0) {
        printf("Could not seed the random number generator: -0x%x\n", -ret);
        return -1;
    }

    if ((ret = mbedtls_x509_crt_parse(&cacert, (const unsigned char *)cert, sizeof(cert))) != 0) {
        printf("Could not load certificates: -0x%x\n", -ret);
        return -1;
    }

    if ((ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        printf("TLS configuration failed: -0x%x\n", -ret);
        return -1;
    }

    if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
        printf("SSL setup failed: -0x%x\n", -ret);
        return -1;
    }
    is_initialized = 1;
    return 0;
}

void downloader_deinit(void)
{
    if (!is_initialized) {
        return;
    }
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_net_free(&server_fd);
    is_initialized = 0;
}

int downloader_check_update(char latest_version[64 + 1], char latest_sha[64 + 1], char **download_url)
{
    int ret, content_length = 0, status = -1;
    char *header = NULL, *content = NULL, *request_buffer = NULL;
    json_value *root = NULL;
    json_value *tag_name = NULL, *assets = NULL;
    json_value *sha_digest = NULL, *url = NULL, *name = NULL;

    const int request_buffer_size = 2048;
    request_buffer = malloc(request_buffer_size);
    if (request_buffer == NULL) {
        printf("Memory allocation failed for request buffer.\n");
        goto cleanup_error;
    }

    // Request information about the latest release
    // https://docs.github.com/en/rest/releases/releases?apiVersion=2022-11-28#get-the-latest-release
    snprintf(request_buffer, request_buffer_size, "GET %s HTTP/1.1\r\n"
                                                  "Host: api.github.com\r\n"
                                                  "User-Agent: %s\r\n"
                                                  "Accept: application/vnd.github+json\r\n"
                                                  "X-GitHub-Api-Version: 2022-11-28\r\n"
                                                  "Connection: close\r\n\r\n",
             GITHUB_REPO_URL, USER_AGENT);

    ret = do_request(&server_fd, &ssl, "api.github.com", request_buffer, &header, &content, &content_length);
    if (ret < 0) {
        printf("do_request failed: -0x%x\n", -ret);
        goto cleanup_error;
    }

    // Parse the JSON response
    root = json_parse((const json_char *)content, content_length);
    if (root == NULL) {
        printf("Header:\n\n%s\n\n", header);
        printf("Body:\n\n%s\n\n", content);
        printf("Failed to parse JSON response.\n");
        goto cleanup_error;
    }

    // Pull out required fields from the JSON response
    for (unsigned int i = 0; i < root->u.object.length; i++) {
        if (strcmp(root->u.object.values[i].name, "assets") == 0) {
            assets = root->u.object.values[i].value;
        }
        if (strcmp(root->u.object.values[i].name, "tag_name") == 0) {
            tag_name = root->u.object.values[i].value;
        }
    }
    if (assets == NULL || assets->type != json_array) {
        printf("No assets array found.\n");
        goto cleanup_error;
    }
    if (tag_name == NULL || tag_name->type != json_string) {
        printf("No tag_name found.\n");
        goto cleanup_error;
    }

    // Scan through the assets to find the one we want. Also pull out the SHA digest and download URL.
    for (unsigned int i = 0; i < assets->u.object.length; i++) {
        json_value *asset = assets->u.array.values[i];
        for (unsigned int j = 0; j < asset->u.object.length; j++) {
            json_object_entry *entry = &asset->u.object.values[j];
            if (strcmp(asset->u.object.values[j].name, "name") == 0) {
                if (strcmp(entry->value->u.string.ptr, WANTED_FILE_NAME) != 0) {
                    continue;
                }
                name = entry->value;
            } else if (strcmp(entry->name, "url") == 0) {
                url = entry->value;
            } else if (strcmp(entry->name, "digest") == 0) {
                sha_digest = asset->u.object.values[j].value;
            }
        }
        if (name != NULL) {
            break;
        }
    }

    if (name == NULL) {
        printf("No asset with name '%s' found in the latest release.\n", WANTED_FILE_NAME);
        goto cleanup_error;
    }

    strcpy(latest_version, tag_name->u.string.ptr);
    if (sha_digest && sha_digest->u.string.ptr) {
        // Git ShA digest is in the format "sha256:abcdef1234567890..."
        // We want to extract the part after the colon. If there is no colon, we use the whole string.
        char *p = strstr(sha_digest->u.string.ptr, ":");
        p = p ? (p + 1) : sha_digest->u.string.ptr;
        strcpy(latest_sha, p);
    } else {
        latest_sha[0] = '\0'; // No SHA digest provided
    }
    *download_url = strdup(url->u.string.ptr);
    status = (*download_url) ? 0 : -1;

cleanup_error:
    if (status == -1) {
        free_request(&header, &content);
    }
    if (request_buffer) {
        free(request_buffer);
    }
    if (root) {
        json_value_free(root);
    }
    return status;
}

int downloader_download_update(char *download_url, void **mem, char **downloaded_data, int *downloaded_size, char downloaded_sha[64 + 1])
{
    const int request_buffer_size = 2048;
    char *request_buffer = malloc(request_buffer_size);
    if (request_buffer == NULL) {
        printf("Memory allocation failed for request buffer.\n");
        return -1;
    }

    char *header = NULL, *content = NULL;
    int content_length = 0;
    int ret;

    // Get the release assets
    // https://docs.github.com/en/rest/releases/assets?apiVersion=2022-11-28#get-a-release-asset
    snprintf(request_buffer, request_buffer_size, "GET %s HTTP/1.1\r\n"
                                                  "Host: api.github.com\r\n"
                                                  "User-Agent: %s\r\n"
                                                  "Accept: application/octet-stream\r\n"
                                                  "X-GitHub-Api-Version: 2022-11-28\r\n"
                                                  "Connection: close\r\n\r\n",
             download_url, USER_AGENT);

    ret = do_request(&server_fd, &ssl, "api.github.com", request_buffer, &header, &content, &content_length);
    if (ret < 0) {
        printf("do_request failed: -0x%x\n", -ret);
        goto cleanup;
    }

    // Check if we got a redirect, if so we need to follow it. Otherwise we should have the content already.
    if (strstr(header, "HTTP/1.1 302")) {
        char *redirect_url = NULL;
        char *redirect_host = NULL;

        // Pull out the Location header which contains the redirect URL
        redirect_url = strstr(header, "Location: ");
        if (redirect_url == NULL) {
            printf("No redirect URL found in header.\n");
            goto cleanup;
        }

        // Get the full redirect URL
        redirect_url += 10; // Move past "Location: ".
        char *end = strstr(redirect_url, "\r\n");
        if (end != NULL) {
            *end = '\0'; // Null-terminate the redirect_url string
        }

        // The redirect URL is something like "https://objects.githubusercontent.com/....."
        // We need to extract out the "objects.githubusercontent.com" part separately
        // First we check if it starts with "https://" the move past it
        redirect_host = strstr(redirect_url, "https://");
        if (redirect_host == NULL) {
            redirect_host = redirect_url;
        } else {
            redirect_host += 8; // Move past "https://"
        }
        // Temporarily null-terminate the host part and copy it to a new string
        char *slash = strchr(redirect_host, '/');
        if (slash != NULL) {
            *slash = '\0'; // Null-terminate the host part
        }
        redirect_host = strdup(redirect_host);
        if (redirect_host == NULL) {
            printf("Failed to allocate memory for redirect host.\n");
            goto cleanup;
        }
        *slash = '/'; // Restore the slash for the location

        // Now actually download the file
        snprintf(request_buffer, request_buffer_size,
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: %s\r\n"
                 "Accept: application/octet-stream\r\n"
                 "Connection: close\r\n\r\n",
                 redirect_url, redirect_host, USER_AGENT);

        free_request(&header, &content);
        ret = do_request(&server_fd, &ssl, redirect_host, request_buffer, &header, &content, &content_length);
        free(redirect_host);
        if (ret < 0) {
            printf("do_request failed: -0x%x\n", -ret);
            goto cleanup;
        }
    }

    // Calculate the SHA256 digest of the content
    unsigned char digest[32];
    mbedtls_md_context_t md_ctx;
    mbedtls_md_init(&md_ctx);
    if ((ret = mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0)) != 0) {
        printf("mbedtls_md_setup failed: -0x%x\n", -ret);
        goto cleanup;
    }
    if ((ret = mbedtls_md_starts(&md_ctx)) != 0) {
        printf("mbedtls_md_starts failed: -0x%x\n", -ret);
        goto cleanup;
    }
    if ((ret = mbedtls_md_update(&md_ctx, (const unsigned char *)content, content_length)) != 0) {
        printf("mbedtls_md_update failed: -0x%x\n", -ret);
        goto cleanup;
    }
    if ((ret = mbedtls_md_finish(&md_ctx, digest)) != 0) {
        printf("mbedtls_md_finish failed: -0x%x\n", -ret);
        goto cleanup;
    }
    mbedtls_md_free(&md_ctx);

    *downloaded_data = content;
    *downloaded_size = content_length;
    for (int i = 0; i < 32; i++) {
        sprintf(&downloaded_sha[i * 2], "%02x", digest[i]);
    }
    *mem = header;
    return 0;
cleanup:
    free_request(&header, &content);
    free(request_buffer);
    return -1;
}
