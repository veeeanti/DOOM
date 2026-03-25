#ifndef DISCORD_RPC_WIN32_H
#define DISCORD_RPC_WIN32_H

void I_DiscordRPC_Init(void);
void I_DiscordRPC_Update(void);
void I_DiscordRPC_Shutdown(void);
void I_DiscordRPC_SetSteamJoin(unsigned long long lobby_id,
							   int party_size,
							   int party_max,
							   const char *mode,
							   const char *content);

#endif
