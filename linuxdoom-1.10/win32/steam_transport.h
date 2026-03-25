//----------------------------------------------------------
//            DOOM'93 Steamworks implementation
//
//  This was the best idea I could come up with that I
//  knew I could work with easily and (hopefully) get
//  a viable replacement for the DOS networking code
//  left to us by id Software. Steam has a testing app
//  id (480, Spacewar) that we can use, everyone has it!
//  Lobbies are still broken as far as I know. It is 
//  REALLY starting to piss me off too. lmao.
//
//  copyright Valve Software, written here by veeλnti
//----------------------------------------------------------

#ifndef DOOM_STEAM_TRANSPORT_H
#define DOOM_STEAM_TRANSPORT_H

#include "d_net.h"

#ifdef __cplusplus
extern "C" {
#endif

int STEAM_InitTransport(doomcom_t *doomcom, int argc, char **argv);
void STEAM_ShutdownTransport(void);
int STEAM_IsActive(void);
void STEAM_NetCmd(doomcom_t *doomcom);
const char *STEAM_LastError(void);
int STEAM_EnsureClient(void);
void STEAM_Update(void);
int STEAM_CreatePublicLobby(void);
int STEAM_JoinLobbyById(unsigned long long lobby_id);
int STEAM_RequestLobbyList(void);
int STEAM_IsLobbyListPending(void);
int STEAM_GetLobbyCount(void);
int STEAM_GetLobbyInfo(int index,
					   char *title,
					   int title_size,
					   char *details,
					   int details_size,
					   unsigned long long *lobby_id);

#ifdef __cplusplus
}
#endif

#endif