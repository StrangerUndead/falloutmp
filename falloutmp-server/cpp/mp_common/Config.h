#pragma once
#include "GameId.h"

constexpr auto kMaxPlayers = MAX_PLAYERS;

// The messaging protocol version: change kProtocolPrefixFallout4 (GameId.h)
// on every incompatible protocol change.
constexpr auto kMessagingProtocolVersion = kProtocolPrefixFallout4.data();

// Users with kMessagingProtocolVersion different to server's one must not be
// able to connect. So we use this value as SLikeNet password by default.
constexpr auto kNetworkingPasswordPrefix = kMessagingProtocolVersion;
