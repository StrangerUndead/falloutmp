#include "Fo4WorldBootstrap.h"
#include "libespm/CombineBrowser.h"
#include "libespm/CompressedFieldsCache.h"
#include "libespm/Convert.h"
#include "libespm/LookupResult.h"
#include "libespm/fo4/Fo4Records.h"
#include "LocationalDataUtils.h"
#include <map>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace fo4 {

WorldBootstrapReport BootstrapWorld(const espm::CombineBrowser& br,
                                    espm::CompressedFieldsCache& cache,
                                    Fo4Server& server,
                                    const WorldBootstrapConfig& cfg)
{
  WorldBootstrapReport rep;
  const auto& data = server.Data();

  std::unordered_map<std::string, FormId> kwByEdid;
  for (auto& lr : br.GetDistinctRecordsByType("KYWD")) {
    kwByEdid[lr.rec->GetEditorId(cache)] = lr.ToGlobalId(lr.rec->GetId());
  }
  std::set<FormId> workshopKws, areaKws;
  for (auto& e : cfg.workshopKeywords) {
    if (auto it = kwByEdid.find(e); it != kwByEdid.end())
      workshopKws.insert(it->second);
  }
  for (auto& e : cfg.buildAreaLinkKeywords) {
    if (auto it = kwByEdid.find(e); it != kwByEdid.end())
      areaKws.insert(it->second);
  }

  struct Pending
  {
    FormId refId;
    std::vector<FormId> areaRefs;
  };
  std::vector<Pending> pendingWorkshops;
  std::unordered_map<FormId, espm::fo4::REFR::Data> primitives;

  for (auto& lr : br.GetDistinctRecordsByType("REFR")) {
    auto refr = espm::Convert<espm::fo4::REFR>(lr.rec);
    if (!refr) {
      continue; // ACHR and others share the vector
    }
    ++rep.referencesScanned;
    auto d = refr->GetData(cache);
    if (d.deleted) {
      continue;
    }
    FormId refId = lr.ToGlobalId(lr.rec->GetId());
    FormId baseId = lr.ToGlobalId(d.baseId);

    if (d.primitiveType) {
      primitives[refId] = d;
    }

    if (cfg.registerMapMarkers && d.isMapMarker && !server.Map().Find(refId)) {
      MapMarker m;
      m.refId = refId;
      m.pos = d.pos;
      m.worldOrCell = LocationalDataUtils::GetWorldOrCell(br, lr);
      if (m.worldOrCell == 0) {
        m.worldOrCell = 0x3c;
      }
      m.name = d.mapMarkerName.empty() ? d.editorId : d.mapMarkerName;
      m.type = d.mapMarkerType;
      m.canTravel = (d.mapFlags & 0x2) != 0;
      server.Map().AddMarker(std::move(m));
      ++rep.mapMarkers;
    }

    auto baseLr = br.LookupById(baseId);
    if (!baseLr.rec) {
      continue;
    }
    auto baseType = baseLr.rec->GetType();

    if (cfg.registerLocks && d.lockLevel && *d.lockLevel > 0) {
      if (baseType == "TERM") {
        if (!server.Locks().GetTerminal(refId)) {
          TerminalState t;
          t.level = *d.lockLevel;
          t.locked = true;
          server.Locks().SetTerminal(refId, t);
          ++rep.terminals;
        }
      } else if (!server.Locks().GetLock(refId)) {
        LockState l;
        l.level = *d.lockLevel;
        l.keyId = d.lockKeyId ? lr.ToGlobalId(d.lockKeyId) : 0;
        l.locked = true;
        l.ownerFaction = d.ownerId ? lr.ToGlobalId(d.ownerId) : 0;
        server.Locks().SetLock(refId, l);
        ++rep.locks;
      }
    }

    // Settlement workbenches: the base can be a container (the vanilla
    // WorkshopWorkbench), furniture or activator. Matched by editor id
    // prefix or a workshop keyword.
    if (cfg.registerWorkshops &&
        (baseType == "CONT" || baseType == "FURN" || baseType == "ACTI") &&
        !server.Workshops().Find(refId)) {
      std::string edid = baseLr.rec->GetEditorId(cache);
      bool isWorkshop = edid.rfind(cfg.workshopBaseEditorIdPrefix, 0) == 0;
      if (!isWorkshop) {
        for (auto kw : espm::fo4::GetKeywords(baseLr.rec, cache)) {
          isWorkshop |= workshopKws.count(baseLr.ToGlobalId(kw)) > 0;
        }
      }
      if (isWorkshop) {
        Pending p{ refId, {} };
        for (auto& link : d.linkedRefs) {
          if (areaKws.count(lr.ToGlobalId(link.keywordId))) {
            p.areaRefs.push_back(lr.ToGlobalId(link.refId));
          }
        }
        pendingWorkshops.push_back(p);
        Workshop w;
        w.workbenchRefId = refId;
        w.locationId =
          d.persistLocationId ? lr.ToGlobalId(d.persistLocationId) : 0;
        w.worldOrCell = LocationalDataUtils::GetWorldOrCell(br, lr);
        server.Workshops().AddWorkshop(std::move(w));
        ++rep.workshops;
        continue;
      }
    }

    if (baseType != "FURN") {
      continue;
    }
    auto furn = espm::Convert<espm::fo4::FURN>(baseLr.rec)->GetData(cache);

    if (cfg.registerFrames && furn.isPowerArmorFurniture &&
        !server.PowerArmor().FindFrame(refId)) {
      PowerArmorFrame f;
      f.refId = refId;
      f.baseId = baseId;
      f.pos[0] = d.pos[0];
      f.pos[1] = d.pos[1];
      f.pos[2] = d.pos[2];
      for (auto& c : furn.containerItems) {
        FormId item = baseLr.ToGlobalId(c.formId);
        auto armor = data.FindArmor(item);
        bool isCore = item == cfg.fusionCoreId;
        if ((armor && armor->powerArmorSlot != PowerArmorSlot::None) ||
            isCore) {
          ItemKey key{ item };
          if (server.PowerArmor().CanPutIntoFrame(f, key) == PaError::None) {
            f.contents.Add(key, 1);
          }
        }
        // Leveled lists (LVLI) resolve through F14 at first spawn
      }
      server.PowerArmor().AddFrame(std::move(f));
      ++rep.frames;
      continue;
    }
  }

  // Build areas: primitives linked from the workbench
  for (auto& p : pendingWorkshops) {
    auto w = server.Workshops().Find(p.refId);
    for (auto areaRef : p.areaRefs) {
      auto it = primitives.find(areaRef);
      if (it == primitives.end()) {
        continue;
      }
      auto& prim = it->second;
      WorkshopArea a;
      a.center = prim.pos;
      a.rotZ = prim.rotRadians[2];
      a.halfExtents = prim.primitiveBounds;
      if (*prim.primitiveType == espm::fo4::REFR::PrimitiveType::Sphere) {
        a.shape = WorkshopArea::Shape::Sphere;
        a.radius = prim.primitiveBounds[0];
      }
      w->areas.push_back(a);
      ++rep.buildAreas;
    }
  }

  spdlog::info("Fallout 4 world bootstrap: {} refs, {} frames, {} workshops "
               "({} build areas), {} locks, {} locked terminals, {} map "
               "markers",
               rep.referencesScanned, rep.frames, rep.workshops,
               rep.buildAreas, rep.locks, rep.terminals, rep.mapMarkers);
  return rep;
}

}
