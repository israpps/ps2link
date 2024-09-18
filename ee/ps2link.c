/*********************************************************************
 * Copyright (C) 2003 Tord Lindstrom (pukko@home.se)
 * Copyright (C) 2003,2004 adresd (adresd_ps2dev@yahoo.com)
 * This file is subject to the terms and conditions of the PS2Link License.
 * See the file LICENSE in the main directory of this distribution for more
 * details.
 */

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

#include <kernel.h>
#include <sifrpc.h>
#include <iopheap.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <debug.h>
#include <ps2sdkapi.h>
#include <sio.h>


#include "irx_variables.h"
#include "hostlink.h"
#include "excepHandler.h"
#include "cmdHandler.h"
#include "globals.h"
#ifdef IOPRP
#include <iopcontrol_special.h>
#endif

////////////////////////////////////////////////////////////////////////

// Argv name+path & just path
char elfName[MAXNAMLEN] __attribute__((aligned(16)));
////////////////////////////////////////////////////////////////////////
#define IPCONF_MAX_LEN 64 // Don't reduce even more this value

// Make sure the "cached config" is in the data section
// To prevent it from being "zeroed" on a restart of ps2link
char if_conf[IPCONF_MAX_LEN] __attribute__((section(".data"))) = "";
int if_conf_len __attribute__((section(".data"))) = 0;

////////////////////////////////////////////////////////////////////////

static void printIpConfig(void)
{
    int i;
    int t;

    scr_printf("Net config: ");
    for (t = 0, i = 0; t < 3; t++) {
        scr_printf("%s  ", &if_conf[i]);
        i += strlen(&if_conf[i]) + 1;
    }
    scr_printf("\n");
}

// Parse network configuration from IPCONFIG.DAT
// Note: parsing really should be made more robust...
static int readIpConfigFromFile(char *buf)
{
    int fd;
    int len;

    fd = open("IPCONFIG.DAT", O_RDONLY);

    if (fd < 0) {
        dbgprintf("Error reading ipconfig.dat\n");
        return -1;
    }

    len = read(fd, buf, IPCONF_MAX_LEN - 1); // Let the last byte be '\0'
    close(fd);

    if (len < 0) {
        dbgprintf("Error reading ipconfig.dat\n");
        return -1;
    }

    dbgscr_printf("ipconfig: Read %d bytes\n", len);
    return 0;
}

static int readDefaultIpConfig(char *buf)
{
    int i = 0;

    strncpy(&buf[i], DEFAULT_IP, 15);
    i += strlen(&buf[i]) + 1;
    strncpy(&buf[i], DEFAULT_NETMASK, 15);
    i += strlen(&buf[i]) + 1;
    strncpy(&buf[i], DEFAULT_GW, 15);

    return 0;
}

static void getIpConfig(void)
{
    int i;
    int t;
    char c;
    char buf[IPCONF_MAX_LEN];

    // Clean buffers
    memset(buf, 0x00, IPCONF_MAX_LEN);
    memset(buf, 0x00, IPCONF_MAX_LEN);

    if (readIpConfigFromFile(buf))
        readDefaultIpConfig(buf);

    i = 0;
    // Clear out spaces (and potential ending CR/LF)
    while ((c = buf[i]) != '\0') {
        if ((c == ' ') || (c == '\r') || (c == '\n'))
            buf[i] = '\0';
        i++;
    }

    scr_printf("Saving IP config...\n");
    for (t = 0, i = 0; t < 3; t++) {
        strncpy(&if_conf[i], &buf[i], 15);
        i += strlen(&if_conf[i]) + 1;
    }

    if_conf_len = i;
}

////////////////////////////////////////////////////////////////////////
// Wrapper to load irx module from disc/rom
static void pkoLoadModule(char *path, int argc, char *argv)
{
    int ret;

    ret = SifLoadModule(path, argc, argv);
    if (ret < 0) {
        scr_printf("Could not load module %s: %d\n", path, ret);
        SleepThread();
    }
    dbgscr_printf("Load %s, returned [%d]\n", path, ret);
}

////////////////////////////////////////////////////////////////////////
// Load all the irx modules we need
static void loadModules(void)
{
    int ret;

    if (if_conf_len == 0) {
        pkoLoadModule("rom0:SIO2MAN", 0, NULL);
        pkoLoadModule("rom0:MCMAN", 0, NULL);
        pkoLoadModule("rom0:MCSERV", 0, NULL);
        return;
    }

    dbgscr_printf("Exec poweroff module. (%x,%d) ", (unsigned int)poweroff_irx, size_poweroff_irx);
    SifExecModuleBuffer(poweroff_irx, size_poweroff_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ps2dev9 module. (%x,%d) ", (unsigned int)ps2dev9_irx, size_ps2dev9_irx);
    SifExecModuleBuffer(ps2dev9_irx, size_ps2dev9_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec netman module. (%x,%d) ", (unsigned int)netman_irx, size_netman_irx);
    SifExecModuleBuffer(netman_irx, size_netman_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec smap module. (%x,%d) ", (unsigned int)smap_irx, size_smap_irx);
    SifExecModuleBuffer(smap_irx, size_smap_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ps2ip module. (%x,%d) ", (unsigned int)ps2ip_nm_irx, size_ps2ip_nm_irx);
    SifExecModuleBuffer(ps2ip_nm_irx, size_ps2ip_nm_irx, if_conf_len, &if_conf[0], &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec udptty module. (%x,%d) ", (unsigned int)udptty_irx, size_udptty_irx);
    SifExecModuleBuffer(&udptty_irx, size_udptty_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ioptrap module. (%x,%d) ", (unsigned int)ioptrap_irx, size_ioptrap_irx);
    SifExecModuleBuffer(ioptrap_irx, size_ioptrap_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("Exec ps2link module. (%x,%d) ", (unsigned int)ps2link_irx, size_ps2link_irx);
    SifExecModuleBuffer(ps2link_irx, size_ps2link_irx, 0, NULL, &ret);
    dbgscr_printf("[%d] returned\n", ret);
    dbgscr_printf("All modules loaded on IOP.\n");
}

static void printWelcomeInfo()
{
    scr_printf("\n\n\n\n");
    scr_printf("Welcome to ps2link %s\n", APP_VERSION);
    scr_printf("ps2link loaded at 0x%08X-0x%08X, size: 0x%08X\n", (unsigned int)&__start, (unsigned int)&_end, (unsigned int)&_end - (unsigned int)&__start);
    scr_printf("Initializing...\n");
}

#ifdef IOPRP
extern unsigned char ioprp[];
extern unsigned int size_ioprp;
#endif

static void restartIOP()
{

   SifInitRpc(0);
#ifdef IOPRP
    dbgscr_printf("reset iop (w/IOPRP)\n");
   while(!SifIopRebootBuffer(ioprp, size_ioprp)){};
#else
    dbgscr_printf("reset iop\n");
   while(!SifIopReset(NULL, 0)){};
#endif
   while(!SifIopSync()){};

   SifInitRpc(0);
   sbv_patch_enable_lmb();
   sbv_patch_disable_prefix_check();

    dbgscr_printf("reseted iop\n");
}

////////////////////////////////////////////////////////////////////////

// This function is defined as weak in ps2sdkc, so how
// we are not using time zone, so we can safe some KB
void _ps2sdk_timezone_update() {}
void _libcglue_rtc_update() {} // this function must be ALWAYS overriden for COH-H models

DISABLE_PATCHED_FUNCTIONS(); // Disable the patched functionalities
DISABLE_EXTRA_TIMERS_FUNCTIONS(); // Disable the extra functionalities for timers
PS2_DISABLE_AUTOSTART_PTHREAD(); // Disable pthread functionality


int main(int argc, char *argv[])
{
    sio_puts("# PS2LINK Start");
    SifInitRpc(0);
#ifdef COH
    SifLoadStartModule("rom0:CDVDFSV", 0, NULL, NULL); // its not listed on arcade IOPBTCONF
#endif
    restartIOP();

    init_scr();
    printWelcomeInfo();
    if (if_conf_len == 0) {
        scr_printf("Initial boot, will load config then reset\n");
        getIpConfig();
        pkoReset(); // Will restart execution
    }

    installExceptionHandlers();

    strcpy(elfName, argv[0]);
    dbgscr_printf("argv[0] is %s\n", elfName);

    dbgscr_printf("loading modules\n");
    loadModules();

    scr_printf("Using cached config\n");
    printIpConfig();

    dbgscr_printf("init cmdrpc\n");
    initCmdRpc();
    scr_printf("Ready with %i memory bytes\n", GetMemorySize());

    ExitDeleteThread();
    return 0;
}
