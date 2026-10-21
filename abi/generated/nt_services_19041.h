/* Generated from abi/nt_services_19041.json; DO NOT EDIT.
 * Windows NT system call IDs: profile pinned to one specific NTDLL hash.
 * This is NOT the future native Winux ABI and is not a ring-3 gateway.
 */
#ifndef WINUX_NT_SERVICES_19041_H
#define WINUX_NT_SERVICES_19041_H
enum wx_nt_service_id {
    WX_NT_WAIT_FOR_SINGLE_OBJECT = 4u,
    WX_NT_READ_FILE = 6u,
    WX_NT_WRITE_FILE = 8u,
    WX_NT_SET_EVENT = 14u,
    WX_NT_CLOSE = 15u,
    WX_NT_ALLOCATE_VIRTUAL_MEMORY = 24u,
    WX_NT_QUERY_INFORMATION_PROCESS = 25u,
    WX_NT_FREE_VIRTUAL_MEMORY = 30u,
    WX_NT_QUERY_VIRTUAL_MEMORY = 35u,
    WX_NT_QUERY_SYSTEM_INFORMATION = 54u,
    WX_NT_CREATE_EVENT = 72u,
    WX_NT_QUERY_VOLUME_INFORMATION_FILE = 73u,
    WX_NT_PROTECT_VIRTUAL_MEMORY = 80u,
    WX_NT_OPEN_DIRECTORY_OBJECT = 88u,
    WX_NT_OPEN_SYMBOLIC_LINK_OBJECT = 302u,
    WX_NT_QUERY_SYMBOLIC_LINK_OBJECT = 351u,
};

static unsigned wx_nt_stack_span(unsigned long long service) {
    switch (service) {
        case WX_NT_READ_FILE:
        case WX_NT_WRITE_FILE:
            return 80u;
        case WX_NT_ALLOCATE_VIRTUAL_MEMORY:
        case WX_NT_QUERY_VIRTUAL_MEMORY:
            return 56u;
        case WX_NT_QUERY_INFORMATION_PROCESS:
        case WX_NT_CREATE_EVENT:
        case WX_NT_QUERY_VOLUME_INFORMATION_FILE:
        case WX_NT_PROTECT_VIRTUAL_MEMORY:
            return 48u;
        default: return 8u;
    }
}
#endif
