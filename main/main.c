#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_eth.h"
#include "protocol_examples_common.h"
#include <time.h>
#include <esp_timer.h>
#include <sys/time.h>

#include <esp_https_server.h>
#include "esp_tls.h"
#include "sdkconfig.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "dirent.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include <stdio.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#if CONFIG_EXAMPLE_FATFS_MODE_READ_ONLY
#define EXAMPLE_FATFS_MODE_READ_ONLY true
#else
#define EXAMPLE_FATFS_MODE_READ_ONLY false
#endif

#if CONFIG_FATFS_LFN_NONE
#define EXAMPLE_FATFS_LONG_NAMES false
#else
#define EXAMPLE_FATFS_LONG_NAMES true
#endif

// Mount path for the partition
const char *base_path = "/spiflash";

// Handle of the wear leveling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

/* A simple example that demonstrates how to create GET and POST
 * handlers and start an HTTPS server.
*/

static const char *TAG = "example";

/* An HTTP GET handler for downloading */
static esp_err_t download_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/octet-stream");  // Set the content type to binary data
    
    // Set the Content-Disposition header to indicate attachment
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"downloaded_file.txt\"");
    
    // Here, you can add code to generate or serve the downloadable content
    // For example, you can send a text file for download:
    const char *download_content = "This is the content of the file you are downloading.";
    
    // Send the content as a binary attachment
    httpd_resp_send(req, download_content, strlen(download_content));

    return ESP_OK;
}

/* An HTTP POST handler for uploading the current PC time to the server */
#include <time.h>

static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char buf[1024];
    int ret = -1;
    int remaining = req->content_len;
    int received = 0;
    while (remaining > 0) {
        ret = httpd_req_recv(req, buf + received, MIN(remaining, sizeof(buf)));
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
        received += ret;
        // You can process the received data here if needed
    }

    // Terminate response
    buf[received] = 0;

    // At this point, you've received the data from the client, which should be the PC time.
    ESP_LOGI(TAG, "Response: '%s'", buf);

    // Parse the timestamp from buf (assuming it's in ISO 8601 format)
    struct tm tm;
    if (strptime(buf, "%Y-%m-%dT%H:%M:%S", &tm) == NULL) {
        ESP_LOGE(TAG, "Failed to parse timestamp");
        return ESP_FAIL;
    }

    // Convert the parsed timestamp to a time_t value
    time_t timestamp = mktime(&tm);

    // Set the ESP's system time using settimeofday
    struct timeval tv;
    tv.tv_sec = timestamp;
    tv.tv_usec = 0;

    if (settimeofday(&tv, NULL) == 0) {
        ESP_LOGI(TAG, "System time updated successfully.");
    } else {
        ESP_LOGE(TAG, "Failed to update system time");
        return ESP_FAIL;
    }

    // Create a message that includes the extracted time
    char time_message[64];
    strftime(time_message, sizeof(time_message), "The updated time is %Y-%m-%d %H:%M:%S", &tm);

    // Send a response back to the client (PC)
    httpd_resp_send(req, time_message, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

static esp_err_t upload_photo(httpd_req_t *req)
{
    char buf[1024];
    int ret = -1;
    
    // Specify the path of the file to read
    const char *file_path = "/spiflash/pacman_logo.png";
    
    // Open the file for reading
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", file_path);
        return ESP_FAIL;
    }

    // Get the file size
    struct stat st;
    if (fstat(fileno(file), &st) == -1) {
        ESP_LOGE(TAG, "Failed to get file size");
        fclose(file);
        return ESP_FAIL;
    }
    int file_size = st.st_size;

    // Set the Content-Disposition header to indicate attachment
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"pacman_logo.png\"");

    // Read and send the file data in chunks
    while ((ret = fread(buf, 1, sizeof(buf), file)) > 0) {
        if (httpd_resp_send_chunk(req, buf, ret) != ESP_OK) {
            fclose(file);
            ESP_LOGE(TAG, "File sending failed");
            return ESP_FAIL;
        }
    }

    // Close the file
    fclose(file);

    // Finish the response
    if (httpd_resp_send_chunk(req, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "File sending failed to finish");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File sent successfully: %s", file_path);
    
    return ESP_OK;
}

/* An HTTP GET handler */
static esp_err_t root_get_handler(httpd_req_t *req)
{
    // Specify the content type as HTML
    httpd_resp_set_type(req, "text/html");

    // Open the "root.html" file
    int file_fd = open("/spiflash/root.html", O_RDONLY);
    if (file_fd == -1)
    {
        // Handle file open error (e.g., log and return an error response)
        ESP_LOGE(TAG,"Failed to open root");
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Create a buffer for reading the file content
    char htmlBuffer[4096]; // Adjust the size as needed

    // Read the file content into the buffer
    ssize_t file_len = read(file_fd, htmlBuffer, sizeof(htmlBuffer));

    // Close the file
    close(file_fd);

    // Check for file read error
    if (file_len == -1)
    {
        // Handle file read error (e.g., log and return an error response)
        ESP_LOGE(TAG,"Failed to read root");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Send the file content as the response
    httpd_resp_send(req, htmlBuffer, file_len);

    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler
};

static const httpd_uri_t logo = {
    .uri       = "/pacman_logo.png",
    .method    = HTTP_GET,
    .handler   = upload_photo
};

static const httpd_uri_t upload = {
    .uri       = "/upload",
    .method    = HTTP_POST,
    .handler   = upload_post_handler
};

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server");

    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

    extern const unsigned char servercert_start[] asm("_binary_servercert_pem_start");
    extern const unsigned char servercert_end[]   asm("_binary_servercert_pem_end");
    conf.servercert = servercert_start;
    conf.servercert_len = servercert_end - servercert_start;

    extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
    extern const unsigned char prvtkey_pem_end[]   asm("_binary_prvtkey_pem_end");
    conf.prvtkey_pem = prvtkey_pem_start;
    conf.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;

#if CONFIG_EXAMPLE_ENABLE_HTTPS_USER_CALLBACK
    conf.user_cb = https_server_user_callback;
#endif
    esp_err_t ret = httpd_ssl_start(&server, &conf);
    if (ESP_OK != ret) {
        ESP_LOGI(TAG, "Error starting server!");
        return NULL;
    }

    // Set URI handlers
    ESP_LOGI(TAG, "Registering URI handlers");
    httpd_register_uri_handler(server, &root);

    // Register the download handler
    httpd_uri_t download = {
        .uri       = "/download",
        .method    = HTTP_GET,
        .handler   = download_get_handler
    };
    httpd_register_uri_handler(server, &download);

    // Register the upload handler
    httpd_register_uri_handler(server, &upload);
    httpd_register_uri_handler(server, &logo);

    return server;
}

static esp_err_t stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    return httpd_ssl_stop(server);
}

static void disconnect_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server) {
        if (stop_webserver(*server) == ESP_OK) {
            *server = NULL;
        } else {
            ESP_LOGE(TAG, "Failed to stop https server");
        }
    }
}

static void connect_handler(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL) {
        *server = start_webserver();
    }
}

void print_indent(int level, int last) {
    for (int i = 0; i < level; i++) {
        if (i == level - 1) {
            printf(last ? "└─ " : "├─ ");
        } else {
            printf("│  ");
        }
    }
}

void list_files(const char *path, int level) {
    struct dirent *entry;
    DIR *dp = opendir(path);

    if (dp == NULL) {
        perror("opendir");
        return;
    }

    int count = 0;
    while ((entry = readdir(dp))) {
        if (entry->d_type == DT_REG) {
            print_indent(level, count == 0);
            printf("%s --- ", entry->d_name);

            // Get file information, including creation time
            struct stat file_stat;
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            if (stat(full_path, &file_stat) == 0) {
                struct tm *tm_info = localtime(&file_stat.st_ctime);
                char time_str[20];
                strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
                printf("Created: %s\n", time_str);
            }
        } else if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                print_indent(level, count == 0);
                printf("%s/\n", entry->d_name);
                char subdir[PATH_MAX];
                snprintf(subdir, sizeof(subdir), "%s/%s", path, entry->d_name);
                list_files(subdir, level + 1);
            }
        }
        count++;
    }

    closedir(dp);
}

void app_main(void)
{
    static httpd_handle_t server = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "Mounting FAT filesystem");
    // To mount device we need the name of device partition, define base_path
    // and allow format partition in case if it is a new one and was not formatted before
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = false,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(base_path, "storage", &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "List of files and directories:");
    list_files(base_path, 1);

#ifdef CONFIG_EXAMPLE_CONNECT_WIFI
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_WIFI
#ifdef CONFIG_EXAMPLE_CONNECT_ETHERNET
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &connect_handler, &server));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ETHERNET_EVENT_DISCONNECTED, &disconnect_handler, &server));
#endif // CONFIG_EXAMPLE_CONNECT_ETHERNET

    ESP_ERROR_CHECK(example_connect());

    ESP_LOGI(TAG, "Done");
}