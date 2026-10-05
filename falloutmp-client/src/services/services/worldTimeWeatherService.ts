import { ClientContext } from "../context";
import { WorldTimeWeatherMessage } from "../messages/fo4Messages";
import { Fo4MsgType } from "../messages/msgType";

export interface TimeWeatherOptions {
  // Only re-set the game clock when it drifted more than this (game hours);
  // small corrections would make shadows and the Pip-Boy clock jump.
  maxDriftHours?: number;
}

// F25: one server clock and one weather for everyone. The game runs its
// own clock at the server time scale between the 10 s updates; this service
// corrects drift and applies weather changes with the server's transition.
export class WorldTimeWeatherService {
  private lastMsg: WorldTimeWeatherMessage | undefined;
  private lastLocalMs = 0;
  private appliedWeather = 0;
  private appliedTimeScale = -1;
  private readonly maxDriftHours: number;
  corrections = 0;

  constructor(private readonly ctx: ClientContext, opts: TimeWeatherOptions = {}) {
    this.maxDriftHours = opts.maxDriftHours ?? 0.05;
    ctx.router.on(Fo4MsgType.WorldTimeWeather, (m) => this.onTimeWeather(m));
  }

  // Server game hour estimated for "now" from the last update.
  estimatedGameHour(): number | undefined {
    if (!this.lastMsg) {
      return undefined;
    }
    const elapsedRealMs = this.ctx.platform.nowMs() - this.lastLocalMs;
    const hours = this.lastMsg.gameHour + (elapsedRealMs / 3600000) * this.lastMsg.timeScale;
    return ((hours % 24) + 24) % 24;
  }

  current(): WorldTimeWeatherMessage | undefined {
    return this.lastMsg;
  }

  reset(): void {
    this.lastMsg = undefined;
    this.appliedWeather = 0;
    this.appliedTimeScale = -1;
  }

  private onTimeWeather(m: WorldTimeWeatherMessage): void {
    const p = this.ctx.platform;
    const estimate = this.estimatedGameHour();
    if (estimate === undefined || hourDistance(estimate, m.gameHour) > this.maxDriftHours) {
      p.setGameTime(m.gameDays, m.gameHour);
      if (estimate !== undefined) {
        this.corrections++;
      }
    }
    if (m.timeScale !== this.appliedTimeScale) {
      p.setTimeScale(m.timeScale);
      this.appliedTimeScale = m.timeScale;
    }
    if (m.weatherId && m.weatherId !== this.appliedWeather) {
      // The first weather after joining applies instantly.
      p.forceWeather(m.weatherId, this.appliedWeather ? m.transitionSec : 0);
      this.appliedWeather = m.weatherId;
    }
    const prev = this.lastMsg;
    this.lastMsg = m;
    this.lastLocalMs = p.nowMs();
    if (!prev || prev.weatherId !== m.weatherId || prev.radstorm !== m.radstorm || prev.timeScale !== m.timeScale) {
      this.ctx.events.emit("timeWeatherChanged", {
        gameDays: m.gameDays,
        gameHour: m.gameHour,
        timeScale: m.timeScale,
        weatherId: m.weatherId,
        radstorm: m.radstorm,
      });
    }
  }
}

function hourDistance(a: number, b: number): number {
  const d = Math.abs(a - b) % 24;
  return Math.min(d, 24 - d);
}
