import { EventEmitter } from 'tsee';

export class StateSet extends EventEmitter<{
    stateChange: (active: boolean, states: string[]) => void
}> {
    private states = new Set<string>();

    constructor() {
        super();
    }

    public add(state: string): void {
        if (!this.states.has(state)) {
            this.states.add(state);
            this.emit('stateChange', true, Array.from(this.states));
        }
    }

    public delete(state: string): void {
        if (this.states.delete(state)) {
            this.emit('stateChange', !this.empty, Array.from(this.states));
        }
    }

    get empty(): boolean {
        return this.states.size === 0;
    }
}