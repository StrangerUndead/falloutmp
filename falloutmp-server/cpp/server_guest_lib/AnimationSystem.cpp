#include "AnimationSystem.h"
#include "AnimationData.h"
#include "MpActor.h"
#include "WorldState.h"

AnimationSystem::AnimationSystem()
{
  animationCallbacks = {
    {
      "blockStart",
      [](MpActor* actor) { actor->SetIsBlockActive(true); },
    },
    {
      "blockStop",
      [](MpActor* actor) { actor->SetIsBlockActive(false); },
    }
  };
}

void AnimationSystem::Init(WorldState* pWorldState)
{
  if (!pWorldState) {
    spdlog::error("No worldState attached to animation system");
    return;
  }
  worldState = pWorldState;
}

void AnimationSystem::Process(MpActor* actor, const AnimationData& animData)
{
  CIString s = animData.animEventName.data();
  auto it = animationCallbacks.find(s);
  if (it == animationCallbacks.end()) {
    return;
  }
  it->second(actor);
}
