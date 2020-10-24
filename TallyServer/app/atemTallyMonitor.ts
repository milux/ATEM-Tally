import { Atem, AtemState } from 'atem-connection';
import { StateSet } from './stateSet';
import { EventEmitter } from 'tsee';
import { TallyState } from './tallyState';

export class AtemTallyMonitor extends EventEmitter<{
    stateChange: (state: TallyState) => void
}> {
    private previewSet = new StateSet();
    private programSet = new StateSet();
    private inPreviewState = false;
    private inProgramState = false;
    currentTallyState = TallyState.INACTIVE;
    private changeListener: (state: AtemState, path: string) => void;

    constructor(private atem: Atem, private chosenInput: number) {
        super();

        this.previewSet.on('stateChange', (active, states) => {
            if (active) {
                console.log(`Preview state enabled (${JSON.stringify(states)})`);
            } else {
                console.log('Preview state disabled');
            }
            this.inPreviewState = active;
            this.updateState();
        });
        this.programSet.on('stateChange', (active, states) => {
            if (active) {
                console.log(`Program state enabled (${JSON.stringify(states)})`);
            } else {
                console.log('Program state disabled');
            }
            this.inProgramState = active;
            this.updateState();
        });

        // console.log(atem.state.video.ME['0'])
        this.changeListener = (_, path) => {
            // Debug output (path)
            if (path.startsWith('video')) {
                console.log('# PATH: ' + path);
            }

            // Background Program
            if (path === 'video.ME.0.programInput') {
                this.handleProgramInput();
            }
            // Background Preview
            if (path === 'video.ME.0.previewInput') {
                this.handlePreviewInput();
            }
            // Transitions (Preview is Program state)
            if (path === 'video.ME.0.transition') {
                this.handleTransitionBackground();
                this.handleUskTransition();
            }
        
            // USK: Preview (by bitmask on transitionProperties.selection)
            if (path === 'video.ME.0.transitionProperties') {
                this.handleUskPreview();
            }
            // USK: OnAir
            const uskOnAir = /^video\.ME\.0\.upstreamKeyers\.(\d)/.exec(path);
            if (uskOnAir) {
                this.handleUskPreview();
                this.handleUskOnAir(Number(uskOnAir[1]));
            }
        
            // DSK: Preview & OnAir
            const dskEventPath = /^video\.downstreamKeyers\.(\d)/.exec(path);
            if (dskEventPath) {
                this.handleDsk(Number(dskEventPath[1]));
            }
        };
        this.atem.on('stateChanged', this.changeListener);

        console.log(`Setup tally for input # ${this.chosenInput}`);

        this.handleProgramInput();
        this.handlePreviewInput();
        this.handleTransitionBackground();
        this.handleUskPreview();
        this.handleUskTransition();
        for (let i = 0; i < 4; i++) {
            this.handleUskOnAir(i);
        }
        for (let i = 0; i < 2; i++) {
            this.handleDsk(i);
        }

        console.log(`Tally ready for input # ${this.chosenInput}`);
    }

    destroy() {
        this.atem.off('stateChanged', this.changeListener);
    }

    get me() {
        return this.atem.state.video.ME['0'];
    }

    updateState() {
        let newState;
        if (this.inProgramState) {
            newState = TallyState.PROGRAM;
        } else if (this.inPreviewState) {
            newState = TallyState.PREVIEW;
        } else {
            newState = TallyState.INACTIVE;
        }
        if (newState !== this.currentTallyState) {
            this.currentTallyState = newState;
            this.emit('stateChange', newState);
        }
    }

    handleProgramInput() {
        if (this.me.programInput === this.chosenInput) {
            this.programSet.add('programInput');
        } else {
            this.programSet.delete('programInput');
        }
        console.log("Program input: " + this.me.programInput);
    }

    handlePreviewInput() {
        if (this.me.previewInput === this.chosenInput) {
            this.previewSet.add('previewInput');
        } else {
            this.previewSet.delete('previewInput');
        }
        console.log("Preview input: " + this.me.previewInput);
    }

    handleTransitionBackground() {
        const selection = this.me.transitionProperties.selection;
        if (this.me.inTransition && (selection & 1) && this.me.previewInput === this.chosenInput) {
            this.programSet.add('transition');
        } else {
            this.programSet.delete('transition');
        }
    }

    handleUskTransition() {
        const selection = (this.me.transitionProperties.selection | 0);
        for (let i = 0; i < 4; i++) {
            if(selection & (2 << i)) {
                const usk = this.me.upstreamKeyers[i];
                if (usk.fillSource === this.chosenInput || usk.cutSource === this.chosenInput) {
                    if (this.me.inTransition) {
                        this.programSet.add(`uskTransition.${i}`);
                    } else {
                        this.programSet.delete(`uskTransition.${i}`);
                    }
                } else {
                    this.programSet.delete(`uskTransition.${i}`);
                }
            } else {
                this.programSet.delete(`uskTransition.${i}`);
            }
        }
        // console.log(this.me.transitionProperties);
    }

    handleUskPreview() {
        const selection = (this.me.transitionProperties.selection | 0);
        for (let i = 0; i < 4; i++) {
            if(selection & (2 << i)) {
                const usk = this.me.upstreamKeyers[i];
                if (usk.fillSource === this.chosenInput || usk.cutSource === this.chosenInput) {
                    this.previewSet.add(`usk.${i}`);
                } else {
                    this.previewSet.delete(`usk.${i}`);
                }
            } else {
                this.previewSet.delete(`usk.${i}`);
            }
        }
        // console.log(this.me.transitionProperties);
    }

    handleUskOnAir(uskIndex: number) {
        const usk = this.me.upstreamKeyers[uskIndex];
        if (usk.onAir && (usk.fillSource === this.chosenInput || usk.cutSource === this.chosenInput)) {
            this.programSet.add(`usk.${uskIndex}`);
        } else {
            this.programSet.delete(`usk.${uskIndex}`);
        }
    }

    handleDsk(dskIndex: number) {
        const dsk = this.atem.state.video.downstreamKeyers[dskIndex];
        const dskRelevant = dsk.sources.fillSource === this.chosenInput || dsk.sources.cutSource === this.chosenInput;
        if (dsk.properties.tie && dskRelevant) {
            this.previewSet.add(`dsk.${dskIndex}`);
        } else {
            this.previewSet.delete(`dsk.${dskIndex}`);
        }
        if (dsk.onAir && dskRelevant) {
            this.programSet.add(`dsk.${dskIndex}`);
        } else {
            this.programSet.delete(`dsk.${dskIndex}`);
        }
    }
}