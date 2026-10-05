#pragma once
#include "GameId.h"

constexpr auto kMaxPlayers = MAX_PLAYERS;

// This value should be increased each time messaging protocol changes
// regardless of is it a major or minor change. Every change is considered
// incompatible to keep protocol versions system maintainable.
constexpr auto kMessagingProtocolVersion = "7_";
static_assert(kProtocolPrefixSkyrim == std::string_view(kMessagingProtocolVersion));

// Users with kMessagingProtocolVersion different to server's one must not be
// able to connect. So we use this value as SLikeNet password by default.
constexpr auto kNetworkingPasswordPrefix = kMessagingProtocolVersion;
