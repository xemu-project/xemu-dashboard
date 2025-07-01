#include <nxdk/net.h>
#include <windows.h>

#include "main.h"
#include <ftpd/ftp.h>
#include <lwip/api.h>
#include <lwip/apps/sntp.h>
#include <lwip/dhcp.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>

static HANDLE network_thread_handle = NULL;
static HANDLE ftp_thread_handle = NULL;

static DWORD WINAPI ftp_thread(LPVOID lpParameter)
{
    (void) lpParameter;
    ftp_server();
    return 0;
}

static DWORD WINAPI network_thread(LPVOID lpParameter)
{
    (void) lpParameter;
    nxNetInit(NULL);
    ftp_thread_handle = CreateThread(NULL, 0, ftp_thread, NULL, 0, NULL);
    SetThreadPriority(ftp_thread_handle, THREAD_PRIORITY_ABOVE_NORMAL);

    return 0;
}

void network_initialise(void)
{
    network_thread_handle = CreateThread(NULL, 0, network_thread, NULL, 0, NULL);
}

void network_get_status(char *ip_address_buffer, char buffer_length)
{
    struct netif *netif = netif_default;
    if (netif == NULL || !netif_is_up(netif)) {
        strncpy(ip_address_buffer, "Not initialised", buffer_length);
    } else if (!netif_is_link_up(netif)) {
        strncpy(ip_address_buffer, "No Link", buffer_length);
    } else if (dhcp_supplied_address(netif) == 0) {
        strncpy(ip_address_buffer, "Waiting for DHCP", buffer_length);
    } else {
        strncpy(ip_address_buffer, "Active", buffer_length);
    }
}
