// Minimal typed event emitter. The game runtime has no Node "events"
// module, so the client carries its own.
export type Listener<A> = (arg: A) => void;

export class EventBus<Events extends { [K in keyof Events]: unknown }> {
  private listeners: { [K in keyof Events]?: Listener<Events[K]>[] } = {};

  on<K extends keyof Events>(name: K, fn: Listener<Events[K]>): () => void {
    const list = this.listeners[name] ?? (this.listeners[name] = []);
    list.push(fn);
    return () => this.off(name, fn);
  }

  once<K extends keyof Events>(name: K, fn: Listener<Events[K]>): () => void {
    const off = this.on(name, (arg) => {
      off();
      fn(arg);
    });
    return off;
  }

  off<K extends keyof Events>(name: K, fn: Listener<Events[K]>): void {
    const list = this.listeners[name];
    if (!list) {
      return;
    }
    const i = list.indexOf(fn);
    if (i >= 0) {
      list.splice(i, 1);
    }
  }

  // A throwing listener must not stop the others or the network loop.
  emit<K extends keyof Events>(name: K, arg: Events[K]): void {
    const list = this.listeners[name];
    if (!list) {
      return;
    }
    for (const fn of list.slice()) {
      try {
        fn(arg);
      } catch (e) {
        this.onListenerError(name as string, e);
      }
    }
  }

  listenerCount<K extends keyof Events>(name: K): number {
    return this.listeners[name]?.length ?? 0;
  }

  onListenerError: (event: string, error: unknown) => void = () => {};
}
