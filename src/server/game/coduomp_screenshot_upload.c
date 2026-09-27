/* NOT_FROM_ORIGINAL_SOURCE (whole file): master-branch remote-screenshot
 * anti-cheat feature. Receives the chunked upload a client sends in response
 * to the server-issued capture request (see
 * src/client/engine/client/coduomp_remote_screenshot.c and
 * src/server/engine/server_operator_screenshot.c) and writes it to disk for
 * an admin to review.
 *
 * These three client commands (ssbegin/ssdata/ssend) are hooked into
 * ClientCommand's "always available" section in client_commands.c, the same
 * plain if-chain that already dispatches "say"/"score"/etc, rather than into
 * the engine's sv_clientCommandHandlers table: that table's original size is
 * asserted in src/qcommon/server_runtime_types.h, so it must not grow.
 *
 * Per-upload state lives in parallel arrays indexed by client slot rather
 * than as new gclient_t fields, matching the same non-invasive pattern used
 * on the engine side. Every path written here is composed only from
 * server-known integers (client slot, request id, level time) -- never from
 * client-supplied text -- so there is no client-controlled path segment to
 * validate. */

#include "coduomp_screenshot_upload.h"
#include "g_syscalls.h"
#include "game_globals.h"
#include "qcommon/coduomp_base64.h"
#include "qcommon/com_sprintf.h"
#include "qcommon/q_shared_types.h"
#include "qcommon/qcommon_limits.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    /* Refuses an upload past this many bytes. Matches the client-side
     * capture cap in coduomp_remote_screenshot.c; enforced independently
     * here since the upload is what actually consumes server disk space. */
    CODUOMP_SCREENSHOT_UPLOAD_MAX_BYTES = 2 * 1024 * 1024,
    CODUOMP_SCREENSHOT_DECODE_SCRATCH_BYTES = 900
};

void Com_Printf(const char *format, ...);

static int32_t coduompUploadHandle[MAX_CLIENTS];
static int32_t coduompUploadRequestId[MAX_CLIENTS];
static int32_t coduompUploadBytes[MAX_CLIENTS];
static qboolean coduompUploadOverflowed[MAX_CLIENTS];

static void coduomp_ScreenshotUpload_Abort(int32_t clientNum)
{
    if (coduompUploadHandle[clientNum] != 0) {
        trap_FS_FCloseFile(coduompUploadHandle[clientNum]);
    }
    coduompUploadHandle[clientNum] = 0;
    coduompUploadRequestId[clientNum] = 0;
    coduompUploadBytes[clientNum] = 0;
    coduompUploadOverflowed[clientNum] = qfalse;
}

/* ssbegin <requestId> <totalBytes> */
void Cmd_ScreenshotBegin_f(gentity_t *ent)
{
    if (g_allowRemoteScreenshot.integer == 0) {
        return;
    }

    const int32_t clientNum = (int32_t)(ent - g_entities);
    coduomp_ScreenshotUpload_Abort(clientNum);

    char requestIdText[MAX_TOKEN_CHARS];
    trap_Argv(1, requestIdText, sizeof(requestIdText));
    const int32_t requestId = atoi(requestIdText);

    char path[64];
    Com_sprintf(path, sizeof(path), "screenshots/remote/pending/client%d.jpg",
                clientNum);

    int32_t handle = 0;
    if (trap_FS_FOpenFile(path, &handle, FS_WRITE) < 0 || handle == 0) {
        Com_Printf(
            "coduomp remote screenshot: couldn't open pending file for "
            "client %d\n",
            clientNum);
        return;
    }

    coduompUploadHandle[clientNum] = handle;
    coduompUploadRequestId[clientNum] = requestId;
    coduompUploadBytes[clientNum] = 0;
    coduompUploadOverflowed[clientNum] = qfalse;
}

/* ssdata <requestId> <base64chunk> */
void Cmd_ScreenshotData_f(gentity_t *ent)
{
    const int32_t clientNum = (int32_t)(ent - g_entities);
    if (g_allowRemoteScreenshot.integer == 0 ||
        coduompUploadHandle[clientNum] == 0) {
        return;
    }

    char requestIdText[MAX_TOKEN_CHARS];
    trap_Argv(1, requestIdText, sizeof(requestIdText));
    if (atoi(requestIdText) != coduompUploadRequestId[clientNum]) {
        return;
    }

    if (coduompUploadOverflowed[clientNum] != qfalse) {
        return;
    }

    char encoded[MAX_STRING_CHARS];
    trap_Argv(2, encoded, sizeof(encoded));

    uint8_t decoded[CODUOMP_SCREENSHOT_DECODE_SCRATCH_BYTES];
    const int32_t decodedBytes =
        coduomp_Base64Decode(encoded, decoded, sizeof(decoded));
    if (decodedBytes < 0) {
        Com_Printf(
            "coduomp remote screenshot: malformed chunk from client %d, "
            "aborting upload\n",
            clientNum);
        coduomp_ScreenshotUpload_Abort(clientNum);
        return;
    }

    if (coduompUploadBytes[clientNum] + decodedBytes >
        CODUOMP_SCREENSHOT_UPLOAD_MAX_BYTES) {
        Com_Printf(
            "coduomp remote screenshot: client %d exceeded the upload "
            "size limit, discarding the rest\n",
            clientNum);
        coduompUploadOverflowed[clientNum] = qtrue;
        return;
    }

    trap_FS_Write(decoded, decodedBytes, coduompUploadHandle[clientNum]);
    coduompUploadBytes[clientNum] += decodedBytes;
}

/* ssend <requestId> */
void Cmd_ScreenshotEnd_f(gentity_t *ent)
{
    const int32_t clientNum = (int32_t)(ent - g_entities);
    if (coduompUploadHandle[clientNum] == 0) {
        return;
    }

    char requestIdText[MAX_TOKEN_CHARS];
    trap_Argv(1, requestIdText, sizeof(requestIdText));
    if (atoi(requestIdText) != coduompUploadRequestId[clientNum]) {
        return;
    }

    trap_FS_FCloseFile(coduompUploadHandle[clientNum]);
    const qboolean overflowed = coduompUploadOverflowed[clientNum];
    const int32_t bytesWritten = coduompUploadBytes[clientNum];
    coduompUploadHandle[clientNum] = 0;
    coduompUploadRequestId[clientNum] = 0;
    coduompUploadBytes[clientNum] = 0;
    coduompUploadOverflowed[clientNum] = qfalse;

    if (overflowed != qfalse) {
        /* Leave the oversized capture under pending/ rather than renaming it
         * into the reviewed set; an operator can still inspect it by hand. */
        Com_Printf(
            "coduomp remote screenshot: upload from client %d (%s) "
            "kept under screenshots/remote/pending/ (too large)\n",
            clientNum, ent->client != NULL ? ent->client->cleanName : "?");
        return;
    }

    char pendingPath[64];
    Com_sprintf(pendingPath, sizeof(pendingPath),
                "screenshots/remote/pending/client%d.jpg", clientNum);
    char finalPath[80];
    Com_sprintf(finalPath, sizeof(finalPath),
                "screenshots/remote/client%d_%d.jpg", clientNum, level.time);
    trap_FS_Rename(pendingPath, finalPath);

    Com_Printf(
        "coduomp remote screenshot: received %d bytes from client %d "
        "(%s), saved to %s\n",
        bytesWritten, clientNum,
        ent->client != NULL ? ent->client->cleanName : "?", finalPath);
}
