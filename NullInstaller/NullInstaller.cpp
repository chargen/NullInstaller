#include "stdafx.h"
#include <initguid.h>
#include <objbase.h>
#include <devguid.h>
#include <assert.h>
#include <setupapi.h>

#define MAX_BUF_SHORT               32

typedef struct _tagInstallParams
{
    DWORD   Flags;
    TCHAR   szPort[MAX_BUF_SHORT];
    TCHAR   szInfName[MAX_PATH];
    TCHAR   szInfSect[LINE_LEN];
    
} INSTALLPARAMS, FAR *LPINSTALLPARAMS;
    
typedef struct tagMODEM_INSTALL_WIZARD
{
    DWORD       cbSize;
    DWORD       Flags;              // MIWF_ bit field
    DWORD       ExitButton;         // PSBTN_ value
    LPARAM      PrivateData;
    INSTALLPARAMS InstallParams;
    
} MODEM_INSTALL_WIZARD, * PMODEM_INSTALL_WIZARD;


#define UM_MAX_BUF_SHORT               32
#define UM_LINE_LEN                   256

#define MIPF_NT4_UNATTEND       0x1
#define MIPF_DRIVER_SELECTED    0x2
 
// Unattended install parameters
typedef struct _tagUMInstallParams
{
    DWORD   Flags;                  // Flags that specify the unattended mode
    WCHAR   szPort[UM_MAX_BUF_SHORT];  // Port on which to install the modem
    WCHAR   szInfName[MAX_PATH];    // for NT4 method, inf name
    WCHAR   szSection[UM_LINE_LEN];    // for NT4 method, section name


} UM_INSTALLPARAMS, *PUM_INSTALLPARAMS, *LPUM_INSTALLPARAMS;

typedef struct tagUM_INSTALL_WIZARD
{
    DWORD            cbSize;             // set to the size of the structure
    DWORD            Reserved1;          // reserved, must be 0
    DWORD            Reserved2;          // reserved, must be 0
    LPARAM           Reserved3;          // reserved, must be 0
    UM_INSTALLPARAMS InstallParams;    // parameters for the wizard

} UM_INSTALL_WIZARD, *PUM_INSTALL_WIZARD, *LPUM_INSTALL_WIZARD;

const WCHAR pszNullModemId[]        = L"PNPC031";
const WCHAR pszNullModemInfFile[]   = L"mdmhayes.inf";
const WCHAR pszComPortRegKey[]      = L"HARDWARE\\DEVICEMAP\\SERIALCOMM";
WCHAR pszPort[]               = L"COM2";
//
// Common allocation
//
PVOID MdmAlloc (DWORD dwSize, BOOL bZero) {
    return LocalAlloc ((bZero) ? LPTR : LMEM_FIXED, dwSize);
}

//
// Common free
//
VOID MdmFree (PVOID pvData) {
    LocalFree(pvData);
}

DWORD MdmInstallNullModem( IN PWCHAR pszPort) ;

int _tmain(int argc, _TCHAR* argv[])
{
	ULONG vrs = GetVersion();

	// Note: On Win10, this will only return 6.2.9200 as max version.  Use Win10 methods to determine exact versions above that
	printf("Microsoft Windows [Version %d.%d.%04d]\n\n",vrs&0xFF,(vrs>>8)&0xFF,(vrs>>16)&0x3FFF);

	if(argc<2)
	{
		printf("Usage: NULLINSTALLER [COM Port<e.g.COM2>]\n");
		return 0;
	}

	printf("Installing NULL Modem Components...");

	DWORD dw = MdmInstallNullModem(argv[1]);

	printf("Done.\n\n");

	return 0;
}

DWORD MdmInstallNullModem(
        IN PWCHAR pszPort) 
{
    GUID Guid = GUID_DEVCLASS_MODEM;
    SP_DEVINFO_DATA deid;
    SP_DEVINSTALL_PARAMS deip;
    SP_DRVINFO_DATA drid;
    UM_INSTALL_WIZARD miw = {sizeof(UM_INSTALL_WIZARD), 0};
    SP_INSTALLWIZARD_DATA  iwd;
	PWCHAR pszTempId = NULL;
    DWORD dwSize, dwErr = NO_ERROR;
    HDEVINFO hdi;
    BOOL bOk;

    // Create the device info list
    hdi = SetupDiCreateDeviceInfoList (&Guid, NULL);
    if (hdi == INVALID_HANDLE_VALUE)
        return ERROR_CAN_NOT_COMPLETE;

    __try {
        // Create a new devinfo.
        deid.cbSize = sizeof(SP_DEVINFO_DATA);
        bOk = SetupDiCreateDeviceInfo (
                    hdi, 
                    pszNullModemId, 
                    &Guid, 
                    NULL, 
                    NULL,
                    DICD_GENERATE_ID,
                    &deid);
        if (bOk == FALSE)
        {
            dwErr = ERROR_CAN_NOT_COMPLETE;
            __leave;
        }

        // Obtain hardware id and init multi-sz buffer
		//
        dwSize = sizeof(pszNullModemId) + (2*sizeof(WCHAR));
        pszTempId = (PWCHAR) MdmAlloc(dwSize * sizeof(WCHAR), FALSE);

        if(NULL == pszTempId)
        {
            dwErr = ERROR_NOT_ENOUGH_MEMORY;
            __leave;
        }
        
        lstrcpyn(pszTempId, pszNullModemId, dwSize);
        pszTempId[lstrlen(pszNullModemId) + 1] = L'\0';
        bOk = SetupDiSetDeviceRegistryProperty(
                hdi, 
                &deid, 
                SPDRP_HARDWAREID,
                (LPBYTE)pszTempId,
                dwSize);
        if (bOk == FALSE)
        {
            dwErr = GetLastError();
            __leave;
        }
            
        // We can let Device Installer Api know that we want 
        // to use a single inf
        //
        deip.cbSize = sizeof(deip);
        bOk = SetupDiGetDeviceInstallParams(
                hdi, 
                &deid, 
                &deip);
        if (bOk == FALSE)
        {
            dwErr = GetLastError();
            __leave;
        }
        
        lstrcpyn(deip.DriverPath, pszNullModemInfFile, MAX_PATH);
        deip.Flags |= DI_ENUMSINGLEINF;

        bOk = SetupDiSetDeviceInstallParams(hdi, &deid, &deip);
        if (bOk == FALSE)
        {
            dwErr = GetLastError();
            __leave;
        }

        // Now we let Device Installer Api build a driver list 
        // based on the information we have given so far.  This 
        // will result in the Inf file being found if it exists 
        // in the usual Inf directory
        //
        bOk = SetupDiBuildDriverInfoList(
                hdi, 
                &deid,
                SPDIT_COMPATDRIVER);
        if (bOk == FALSE)
        {
            dwErr = GetLastError();
            __leave;
        }

        // Make it the selected driver...
		//
        ZeroMemory(&drid, sizeof(drid));
        drid.cbSize = sizeof(drid);
        bOk = SetupDiEnumDriverInfo(
                hdi, 
                &deid,
                SPDIT_COMPATDRIVER, 
                0, 
                &drid);
        if (bOk == FALSE)
        {
            dwErr = GetLastError();
            __leave;
        }

        bOk = SetupDiSetSelectedDriver(
                hdi, 
                &deid, 
                &drid);
        if (bOk == FALSE)
        {
            dwErr = ERROR_CAN_NOT_COMPLETE;
            __leave;
        }

        miw.InstallParams.Flags = MIPF_DRIVER_SELECTED;
        lstrcpyn (miw.InstallParams.szPort, pszPort, UM_MAX_BUF_SHORT);
        ZeroMemory(&iwd, sizeof(iwd));
        iwd.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        iwd.ClassInstallHeader.InstallFunction = DIF_INSTALLWIZARD;
        iwd.hwndWizardDlg = NULL;
        iwd.PrivateData = (LPARAM)&miw;

        bOk = SetupDiSetClassInstallParams (
                hdi, 
                &deid, 
                (PSP_CLASSINSTALL_HEADER)&iwd, 
                sizeof(iwd));
        if (bOk == FALSE)
        {
            dwErr = GetLastError();
            __leave;
        }

        // Call the class installer to invoke the installation wizard...

        bOk = SetupDiCallClassInstaller (
                DIF_INSTALLWIZARD, 
                hdi, 
                &deid);
        if (bOk == FALSE)
        {
            dwErr = GetLastError();
            __leave;
        }

        SetupDiCallClassInstaller (
                DIF_DESTROYWIZARDDATA, 
                hdi, 
                &deid);
    }
    __finally {
        SetupDiDestroyDeviceInfoList (hdi);
        if (pszTempId)
            MdmFree(pszTempId);
    }

    return dwErr;
}
