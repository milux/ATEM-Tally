import { AtemTallyMonitor } from "./atemTallyMonitor";
import { TallyState } from "./tallyState";
import { Atem } from "atem-connection";
import { createServer, Socket } from "net";

const PORT = 7411;

const atem = new Atem();
const monitors = new Map<number, AtemTallyMonitor>();
const usedInputs = new Map<string, number>();
const monitorRefCount = new Map<number, number>();

atem.on('error', console.error);
atem.connect('192.168.77.65');

const sendState = (state: TallyState, socket: Socket) => {
    try {
        const stateData = Uint8Array.from([state]);
        socket.write(stateData);
    } catch(e) {
        console.error(e);
    }
};

atem.on('connected', () => {
    createServer((socket) => {
        const clientIdent = socket.remoteAddress + ':' + socket.remotePort;
        const stateChangeHandler = (state: TallyState) => sendState(state, socket);
        console.log('CONNECTED: ' + clientIdent);

        socket.once('data', function (data) {
            console.log('DATA ' + clientIdent + ': ' + data[0]);
            const input = data[0];
            if (input > 10) {
                throw new Error(`Invalid input # ${input}`);
            } else {
                console.log(`Watching input ${input}`);
                usedInputs.set(clientIdent, input);
                monitorRefCount.set(input, (monitorRefCount.get(input) || 0) + 1)
                let tally = monitors.get(input);
                if (tally === undefined) {
                    tally = new AtemTallyMonitor(atem, input);
                    tally.on('stateChange', (state) => {
                        switch (state) {
                            case TallyState.INACTIVE:
                                console.log(`>>> Input # ${input}: Inactive State`);
                                break;
                            case TallyState.PREVIEW:
                                console.log(`>>> Input # ${input}: Preview State`);
                                break;
                            case TallyState.PROGRAM:
                                console.log(`>>> Input # ${input}: Program State`);
                                break;
                            default:
                                throw new Error('Unknown State!');
                        }
                    });
                    monitors.set(input, tally);
                }
                tally.on('stateChange', stateChangeHandler);
                sendState(tally.currentTallyState, socket);
            }
        });

        socket.on('error', (err) => {
            console.error(err);
        });

        // Add a 'close' event handler to this instance of socket
        socket.on('close', (hadError) => {
            const input = usedInputs.get(clientIdent);
            usedInputs.delete(clientIdent);
            if (input) {
                monitors.get(input)?.off('stateChange', stateChangeHandler);
                const refCount = (monitorRefCount.get(input) || 1) - 1;
                if (refCount === 0) {
                    monitors.get(input)?.destroy()
                    monitors.delete(input);
                    console.log(`Removed monitor for input # ${input}, reference count was 0.`);
                }
            }
            console.log(`CLOSED: ${clientIdent} (${hadError ? 'with error' : 'clean'})`);
        });
    }).listen(PORT, '0.0.0.0', () => {
        console.log('TCP Server is listening on port ' + PORT + '.');
    });
});
