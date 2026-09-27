/* NOT_FROM_ORIGINAL_SOURCE (whole file): master-branch remote-screenshot
 * anti-cheat feature. Lets an rcon admin ask a connected client to capture
 * and upload its current view for manual review, on demand or on an
 * automatic periodic interval. See docs/dedicated-engine.md ("Remote
 * screenshots") for the operator-facing description.
 *
 * The client-facing trigger and the receiving side (which lives in the
 * server game module, src/server/game/coduomp_screenshot_upload.c) are
 * described in src/client/engine/client/coduomp_remote_screenshot.c. This
 * file owns only the server engine's half: the feature cvars, the
 * `requestScreenshot` operator command, and the periodic auto-capture scan.
 *
 * State is kept in parallel arrays indexed by client slot rather than as new
 * client_t fields, because client_t's size is asserted against the original
 * binary layout (src/qcommon/server_runtime_types.h). */

#include "qcommon/q_command.h"
#include "qcommon/q_cvar.h"
#include "qcommon/q_string.h"
#include "qcommon/qcommon_limits.h"
#include "qcommon/server_runtime_types.h"
#include "server_commands.h"
#include "server_operator_clients.h"
#include "server_operator_screenshot.h"

#include <stdint.h>

extern serverStatic_t svs;
extern cvar_t *sv_maxclients;
extern cvar_t *sv_running;

void Com_Printf(const char *format, ...);

/* CVAR_ARCHIVE so an operator's choice survives a restart; CVAR_SYSTEMINFO is
 * deliberately omitted, unlike sv_allowDownload, since clients have no
 * business seeing this toggle in their systeminfo string. */
cvar_t *sv_allowRemoteScreenshot;
/* Seconds between automatic per-client captures; "0" (the default) disables
 * the periodic scan entirely and leaves only the on-demand command. */
cvar_t *sv_autoScreenshotInterval;

static int32_t coduompNextScreenshotRequestId[MAX_CLIENTS];
static int32_t coduompLastAutoScreenshotTime[MAX_CLIENTS];

void coduomp_RemoteScreenshot_RegisterCvars(void)
{
    sv_allowRemoteScreenshot =
        Cvar_Get("sv_allowRemoteScreenshot", "1", CVAR_ARCHIVE);
    sv_autoScreenshotInterval =
        Cvar_Get("sv_autoScreenshotInterval", "0", CVAR_ARCHIVE);
}

/* Sends the 'Z' reliable command (see coduomp_remote_screenshot.c) that asks
 * this client to capture and upload a screenshot. Each client gets its own
 * increasing request id so the upload it eventually sends back can be told
 * apart from a previous or concurrent one purely from what the client
 * echoes; the server keeps no other state tying a client to a request. */
static void coduomp_RemoteScreenshot_SendRequest(client_t *client)
{
    const int32_t clientNum = (int32_t)(client - svs.clients);
    const int32_t requestId = ++coduompNextScreenshotRequestId[clientNum];

    SV_SendServerCommand(client, qtrue, "Z %d", requestId);
}

/* Operator command: requestScreenshot <player name|client number>. Mirrors
 * the SV_GetPlayerByName/SV_GetPlayerByNum lookup pattern used by the kick
 * and ban operator commands in server_operator_clients.c. */
void SV_RequestScreenshot_f(void)
{
    if (sv_running->integer == 0) {
        Com_Printf("Server is not running.\n");
        return;
    }

    if (sv_allowRemoteScreenshot->integer == 0) {
        Com_Printf(
            "Remote screenshots are disabled (sv_allowRemoteScreenshot "
            "0).\n");
        return;
    }

    if (Cmd_Argc() != 2) {
        Com_Printf("Usage: requestScreenshot <player name|client number>\n");
        return;
    }

    /* Route to the same numeric-slot-vs-name lookup the kick/ban operator
     * commands use, so an unmatched name doesn't also print a spurious
     * "Bad slot number" from SV_GetPlayerByNum. */
    qboolean isNumeric = qtrue;
    for (const char *cursor = Cmd_Argv(1); *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            isNumeric = qfalse;
            break;
        }
    }

    client_t *const client =
        isNumeric != qfalse ? SV_GetPlayerByNum() : SV_GetPlayerByName();
    if (client == NULL) {
        return;
    }

    coduomp_RemoteScreenshot_SendRequest(client);
    Com_Printf("Requested a screenshot from %s\n", client->name);
}

/* Called once per server frame from SV_Frame (server_frame.c), after
 * SV_CheckTimeouts. Scans connected clients and re-requests a capture from
 * any client whose interval has elapsed. A "0" interval (the default) makes
 * this a no-op scan, so the feature stays entirely on-demand until an
 * operator opts in. */
void coduomp_RemoteScreenshot_AutoCaptureFrame(void)
{
    const int32_t intervalMsec = sv_autoScreenshotInterval->integer * 1000;
    if (intervalMsec <= 0 || sv_allowRemoteScreenshot->integer == 0) {
        return;
    }

    for (int32_t clientNum = 0; clientNum < sv_maxclients->integer;
         ++clientNum) {
        client_t *const client = &svs.clients[clientNum];
        if (client->state != CS_ACTIVE) {
            continue;
        }

        if (svs.realTime - coduompLastAutoScreenshotTime[clientNum] <
            intervalMsec) {
            continue;
        }

        coduompLastAutoScreenshotTime[clientNum] = svs.realTime;
        coduomp_RemoteScreenshot_SendRequest(client);
    }
}
