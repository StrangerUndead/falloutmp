#pragma once
// Builds the server's Fallout 4 world objects from the load order:
// power armor frames, settlements with their build areas, and locks on
// doors, containers and terminals (F17, F22, F24).
//
// Records already present (loaded from the world save) are left alone, so
// player changes survive restarts.
#include "Fo4Server.h"
#include <set>
#include <string>

namespace espm {
class CombineBrowser;
class CompressedFieldsCache;
}

namespace fo4 {

struct WorldBootstrapConfig
{
  // Settlement workbench detection [verify D-real against Fallout4.esm]:
  // base FURN editor id prefix, or any of these keywords on the base.
  std::string workshopBaseEditorIdPrefix = "WorkshopWorkbench";
  std::set<std::string> workshopKeywords = { "WorkshopKeyword" };
  // Linked-ref keywords on the workbench REFR
  std::set<std::string> buildAreaLinkKeywords = { "WorkshopLinkedPrimitive",
                                                  "WorkshopLinkedBuildArea" };
  FormId fusionCoreId = 0x75FE4;
  bool registerLocks = true;
  bool registerFrames = true;
  bool registerWorkshops = true;
  bool registerMapMarkers = true;
};

struct WorldBootstrapReport
{
  size_t referencesScanned = 0;
  size_t frames = 0;
  size_t workshops = 0;
  size_t buildAreas = 0;
  size_t locks = 0;
  size_t terminals = 0;
  size_t mapMarkers = 0;
};

WorldBootstrapReport BootstrapWorld(const espm::CombineBrowser& browser,
                                    espm::CompressedFieldsCache& cache,
                                    Fo4Server& server,
                                    const WorldBootstrapConfig& config = {});

}
