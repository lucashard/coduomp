/* NOT_FROM_ORIGINAL_SOURCE (whole file): master-branch remote-screenshot
 * anti-cheat feature. An admin can ask a connected client, over rcon, to
 * capture and upload its current view for manual review. See
 * docs/dedicated-engine.md ("Remote screenshots") for the operator-facing
 * description of the wire behavior implemented here.
 *
 * Design notes:
 *  - The trigger ('Z' server command, chosen because no original reliable
 *    server command uses it -- see cg_servercommand.c and
 *    src/client/engine/client/server_commands.c) is handled entirely at the
 *    engine layer in CL_GetServerCommand and never reaches the cgame VM, so
 *    this feature does not touch the recovered uo_cgame_mp_x86.dll dispatch.
 *  - The capture reuses the existing silent JPEG screenshot path
 *    (R_ScreenShotJPEG_f via Cmd_ExecuteString, the same mechanism
 *    cl_avidemo already uses) rather than adding a new renderer capture API.
 *  - The upload rides the existing client->server reliable command channel
 *    (CL_AddReliableCommand) in small base64 chunks, paced one chunk per
 *    client frame while the reliable-command backlog has headroom, instead
 *    of adding a new binary netchan message. This avoids extending
 *    sv_clientCommandHandlers (its original table size is asserted in
 *    src/qcommon/server_runtime_types.h) or any other size-locked recovered
 *    structure. The receiving side lives in the server game module
 *    (src/server/game/coduomp_screenshot_upload.c), reached through the
 *    ordinary GAME_CLIENT_COMMAND path used by chat and votes.
 */

#include "cgame.h"

#include "qcommon/coduomp_base64.h"
#include "qcommon/com_sprintf.h"
#include "qcommon/q_command.h"

#include <stdint.h>
#include <string.h>

enum {
    /* Raw bytes per uploaded chunk before base64 expansion. Encoded length is
     * ceil(700/3)*4 = 936 characters; with the "ssdata <id> " prefix this
     * stays comfortably under CODUO_RELIABLE_COMMAND_CAPACITY (1024). */
    CODUOMP_REMOTE_SCREENSHOT_CHUNK_BYTES = 700,
    /* Refuse to upload a capture bigger than this. A silent JPEG at a high
     * native resolution can run past a megabyte; anti-cheat review does not
     * need full fidelity, and this bounds both memory and upload time. */
    CODUOMP_REMOTE_SCREENSHOT_MAX_BYTES = 2 * 1024 * 1024,
    /* Keep at least this many reliable-command slots free for ordinary
     * traffic (chat, votes, userinfo) while an upload is draining. */
    CODUOMP_REMOTE_SCREENSHOT_RESERVED_SLOTS = CODUO_RELIABLE_COMMAND_COUNT / 2
};

typedef struct coduomp_remoteScreenshotUpload_s {
    qboolean active;
    int32_t requestId;
    uint8_t *buffer;
    int32_t bufferSize;
    int32_t bufferOffset;
} coduomp_remoteScreenshotUpload_t;

static coduomp_remoteScreenshotUpload_t coduompRemoteScreenshotUpload;

void Com_Printf(const char *format, ...);
void Com_DPrintf(const char *format, ...);
int32_t FS_ReadFile(const char *qpath, void **buffer);
void FS_FreeFile(void *buffer);

static void coduomp_RemoteScreenshotUpload_Reset(void)
{
    if (coduompRemoteScreenshotUpload.buffer != NULL) {
        FS_FreeFile(coduompRemoteScreenshotUpload.buffer);
    }
    memset(&coduompRemoteScreenshotUpload, 0,
           sizeof(coduompRemoteScreenshotUpload));
}

/* Called from CL_GetServerCommand when a 'Z' reliable command arrives. Takes
 * a silent local JPEG capture and queues it for chunked upload. Ignores the
 * request if an upload is already in flight, so a flurry of admin requests
 * cannot overlap two captures into one stream. */
void coduomp_RemoteScreenshotUpload_HandleRequest(int32_t requestId)
{
    if (coduompRemoteScreenshotUpload.active != qfalse) {
        Com_DPrintf(
            "coduomp_RemoteScreenshotUpload: ignoring request %d, "
            "upload %d still in progress\n",
            requestId, coduompRemoteScreenshotUpload.requestId);
        return;
    }

    char captureName[64];
    Com_sprintf(captureName, sizeof(captureName),
                "screenshotjpeg silent remote_pending/%d\n", requestId);
    Cmd_ExecuteString(captureName);

    char capturePath[80];
    Com_sprintf(capturePath, sizeof(capturePath),
                "screenshots/remote_pending/%d.jpg", requestId);

    void *fileBuffer = NULL;
    const int32_t fileLength = FS_ReadFile(capturePath, &fileBuffer);
    if (fileLength <= 0 || fileBuffer == NULL) {
        Com_Printf(
            "WARNING: remote screenshot request %d: capture failed\n",
            requestId);
        if (fileBuffer != NULL) {
            FS_FreeFile(fileBuffer);
        }
        return;
    }

    if (fileLength > CODUOMP_REMOTE_SCREENSHOT_MAX_BYTES) {
        Com_Printf(
            "WARNING: remote screenshot request %d: capture too large "
            "to upload (%d bytes)\n",
            requestId, fileLength);
        FS_FreeFile(fileBuffer);
        return;
    }

    coduompRemoteScreenshotUpload.active = qtrue;
    coduompRemoteScreenshotUpload.requestId = requestId;
    coduompRemoteScreenshotUpload.buffer = (uint8_t *)fileBuffer;
    coduompRemoteScreenshotUpload.bufferSize = fileLength;
    coduompRemoteScreenshotUpload.bufferOffset = 0;

    CL_AddReliableCommand(
        va("ssbegin %d %d", requestId, fileLength));
}

/* Called once per client frame (see the coduomp_RemoteScreenshotUpload_Frame
 * call in CL_Frame, src/client/engine/client/cgame_frame.c). Drains at most
 * one chunk per frame while the reliable-command backlog has headroom, so an
 * upload never risks tripping CL_AddReliableCommand's overflow disconnect. */
void coduomp_RemoteScreenshotUpload_Frame(void)
{
    if (coduompRemoteScreenshotUpload.active == qfalse) {
        return;
    }

    if (clc.reliableSequence - clc.reliableAcknowledge >=
        CODUOMP_REMOTE_SCREENSHOT_RESERVED_SLOTS) {
        return;
    }

    if (coduompRemoteScreenshotUpload.bufferOffset >=
        coduompRemoteScreenshotUpload.bufferSize) {
        CL_AddReliableCommand(
            va("ssend %d", coduompRemoteScreenshotUpload.requestId));
        coduomp_RemoteScreenshotUpload_Reset();
        return;
    }

    const int32_t remaining = coduompRemoteScreenshotUpload.bufferSize -
                              coduompRemoteScreenshotUpload.bufferOffset;
    const int32_t chunkBytes =
        remaining < CODUOMP_REMOTE_SCREENSHOT_CHUNK_BYTES
            ? remaining
            : CODUOMP_REMOTE_SCREENSHOT_CHUNK_BYTES;

    char encoded[1200];
    const size_t encodedLength = coduomp_Base64Encode(
        coduompRemoteScreenshotUpload.buffer +
            coduompRemoteScreenshotUpload.bufferOffset,
        (size_t)chunkBytes, encoded, sizeof(encoded));
    if (encodedLength == (size_t)-1) {
        Com_Printf(
            "WARNING: remote screenshot request %d: chunk encode "
            "overflow, aborting upload\n",
            coduompRemoteScreenshotUpload.requestId);
        coduomp_RemoteScreenshotUpload_Reset();
        return;
    }

    CL_AddReliableCommand(
        va("ssdata %d %s", coduompRemoteScreenshotUpload.requestId,
           encoded));
    coduompRemoteScreenshotUpload.bufferOffset += chunkBytes;
}
