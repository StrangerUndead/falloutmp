import { RequestOutcome } from "../../core/requestTracker";
import { ClientContext, failed } from "../context";
import { MapDiscoveryMessage, MapMarkerEntry } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

// F26: map markers known to the player (discovered on the server by
// proximity, persisted per character) and server-validated fast travel.
// The teleport itself arrives through the upstream Teleport message.
export class MapService {
  private markers = new Map<number, MapMarkerEntry>();

  constructor(private readonly ctx: ClientContext) {
    ctx.router.on(Fo4MsgType.MapDiscovery, (m) => this.onDiscovery(m));
  }

  known(): MapMarkerEntry[] {
    return Array.from(this.markers.values());
  }

  isKnown(markerRefId: number): boolean {
    return this.markers.has(markerRefId);
  }

  fastTravel(markerRefId: number): Promise<RequestOutcome> {
    if (!this.markers.has(markerRefId)) {
      this.ctx.reportFailure(Fo4MsgType.FastTravelRequest, "NotDiscovered");
      return Promise.resolve(failed(Fo4MsgType.FastTravelRequest, "NotDiscovered"));
    }
    return this.ctx.request(Fo4MsgType.FastTravelRequest, { markerRefId });
  }

  reset(): void {
    this.markers.clear();
  }

  // Re-shows markers whose references streamed in after the list arrived.
  refreshMarkers(): void {
    for (const m of this.markers.values()) {
      this.show(m);
    }
  }

  private onDiscovery(m: MapDiscoveryMessage): void {
    const discovered: MapMarkerEntry[] = [];
    if (m.full) {
      const incoming = new Set(m.markers.map((x) => x.refId));
      for (const [id] of Array.from(this.markers.entries())) {
        if (!incoming.has(id)) {
          const local = this.ctx.platform.refs.toLocal(id);
          if (local) this.ctx.platform.setMapMarker(local, false, false);
          this.markers.delete(id);
        }
      }
    }
    for (const marker of m.markers) {
      const isNew = !this.markers.has(marker.refId);
      this.markers.set(marker.refId, marker);
      this.show(marker);
      if (isNew && !m.full) {
        discovered.push(marker);
        if (marker.name) {
          this.ctx.platform.showNotification(`Discovered: ${marker.name}`);
        }
      }
    }
    this.ctx.events.emit("mapMarkersChanged", { markers: this.known(), discovered });
  }

  private show(marker: MapMarkerEntry): void {
    const local = this.ctx.platform.refs.toLocal(marker.refId);
    if (local) {
      this.ctx.platform.setMapMarker(local, true, true);
    }
  }
}
