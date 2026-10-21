/* Original minimal Win32 executable: same binary is tested on Windows and Winux. */
typedef unsigned long DWORD;
typedef int BOOL;
typedef void *HANDLE;

__declspec(dllimport) HANDLE __stdcall GetStdHandle(DWORD kind);

__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE file, const void *data, DWORD bytes, DWORD *written,
                                               void *overlapped);

__declspec(dllimport) DWORD __stdcall GetLastError(void);

static const char message[] = "Hello from the same Win32 PE binary!\r\n";
/* A relocation-bearing pointer verifies the loader's DIR64 path. */
static const char *volatile text = message;

int mainCRTStartup(void) {
    DWORD written = 0;
    if (WriteFile((HANDLE) 0, text, 3, &written, 0) || written != 0 || GetLastError() != 6)return 3;
    if (!WriteFile(GetStdHandle((DWORD) -11), text, sizeof(message) - 1, &written, 0))return 1;
    return written == sizeof(message) - 1 ? 0 : 2;
}
