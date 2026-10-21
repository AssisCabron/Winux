/* Host contract test for the same generated header consumed by the Winux kernel. */
#include "../../abi/generated/nt_services_19041.h"

int main(void) {
    if (WX_NT_WAIT_FOR_SINGLE_OBJECT != 4u) return 1;
    if (WX_NT_READ_FILE != 6u || WX_NT_WRITE_FILE != 8u) return 2;
    if (WX_NT_ALLOCATE_VIRTUAL_MEMORY != 24u ||
        WX_NT_FREE_VIRTUAL_MEMORY != 30u ||
        WX_NT_QUERY_VIRTUAL_MEMORY != 35u) return 3;
    if (WX_NT_CREATE_EVENT != 72u || WX_NT_QUERY_VOLUME_INFORMATION_FILE != 73u) return 4;
    if (WX_NT_QUERY_SYMBOLIC_LINK_OBJECT != 351u) return 5;
    if (wx_nt_stack_span(WX_NT_READ_FILE) != 80u ||
        wx_nt_stack_span(WX_NT_WRITE_FILE) != 80u) return 6;
    if (wx_nt_stack_span(WX_NT_ALLOCATE_VIRTUAL_MEMORY) != 56u ||
        wx_nt_stack_span(WX_NT_QUERY_VIRTUAL_MEMORY) != 56u) return 7;
    if (wx_nt_stack_span(WX_NT_QUERY_INFORMATION_PROCESS) != 48u ||
        wx_nt_stack_span(WX_NT_CREATE_EVENT) != 48u ||
        wx_nt_stack_span(WX_NT_QUERY_VOLUME_INFORMATION_FILE) != 48u ||
        wx_nt_stack_span(WX_NT_PROTECT_VIRTUAL_MEMORY) != 48u) return 8;
    if (wx_nt_stack_span(WX_NT_CLOSE) != 8u ||
        wx_nt_stack_span(0xffffffffu) != 8u) return 9;
    return 0;
}
