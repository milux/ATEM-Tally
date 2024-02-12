import { TallyState } from "./tallyState";
import { Atem } from "atem-connection";
import { createServer, Socket } from "net";
import { lookup } from "dns";

const PORT = 7411;
const ATEM_DNS = 'atem.internal';

const atem = new Atem();
const lastStates = new Map<number, TallyState>();
const socketSets = new Map<number, Set<Socket>>();
const newFormatSockets = new Set<Socket>();
const usedInputs = new Map<string, Set<number>>();
const legacyMap: Map<number, number> = new Map([
    [4, 1],
    [5, 2],
    [3, 3]
]);

atem.on('error', console.error);
atem.on('info', console.log);
atem.on('debug', console.log);

// Manually resolve IP address to prevent epic reconnect fuckup of atem-connect
const doLookup = () => {
    lookup(ATEM_DNS, (error, address) => {
        if (error) {
            console.error(`Error on DNS lookup for ${ATEM_DNS}, retry after 1 second, error message: `, error);
            setTimeout(doLookup, 1000);
        } else {
            console.log(`${ATEM_DNS} resolved to ${address}, connecting ATEM...`)
            atem.connect(address);
        }
    });
};

const sendState = (cam: number, state: TallyState, socket: Socket) => {
    try {
        if (newFormatSockets.has(socket)) {
            // For new format sockets: Send camera number and state
            socket.write(Uint8Array.from([cam, state]));
        } else if (state === TallyState.PREVIEW_PROGRAM) {
            // For old format sockets: Convert PREVIEW_PROGRAM to PROGRAM
            socket.write(Uint8Array.from([TallyState.PROGRAM]));
        } else {
            socket.write(Uint8Array.from([state]));
        }
    } catch(e) {
        console.error(e);
    }
};

const getState = (input: number): TallyState => {
    const previewSet = new Set(atem.listVisibleInputs("preview"));
    const programSet = new Set(atem.listVisibleInputs("program"));
    if (programSet.has(input)) {
        if (previewSet.has(input)) {
            return TallyState.PREVIEW_PROGRAM
        } else {
            return TallyState.PROGRAM;
        }
    } else if (previewSet.has(input)) {
        return TallyState.PREVIEW;
    } else {
        return TallyState.INACTIVE;
    }
}

const updateStates = () => {
    const previewSet = new Set(atem.listVisibleInputs("preview"));
    const programSet = new Set(atem.listVisibleInputs("program"));
    // console.log("previews", previewSet);
    // console.log("programs", programSet);
    const handleState = (cam: number, state: TallyState, stateDescription: string) => {
        if (lastStates.get(cam) !== state) {
            console.log(`>>> Input # ${cam}: ${stateDescription}`);
            lastStates.set(cam, state);
            socketSets.get(cam)?.forEach((socket) => sendState(cam, state, socket));
        }
    };
    for (const cam of Array.from(socketSets.keys())) {
        if (programSet.has(cam)) {
            if (previewSet.has(cam)) {
                handleState(cam, TallyState.PREVIEW_PROGRAM, "Preview & Program State");
            } else {
                handleState(cam, TallyState.PROGRAM, "Program State");
            }
        } else if (previewSet.has(cam)) {
            handleState(cam, TallyState.PREVIEW, "Preview State");
        } else {
            handleState(cam, TallyState.INACTIVE, "Inactive State");
        }
    }
};

atem.on('connected', () => {
    atem.on('stateChanged', updateStates);

    createServer((socket) => {
        const clientIdent = `${socket.remoteAddress}:${socket.remotePort}`;
        console.log(`CONNECTED: ${clientIdent}`);

        // Disable sending delay to improve responsiveness
        socket.setNoDelay(true);

        socket.once('data', (data) => {
            console.log(`DATA on ${clientIdent}: ${(data).toString('hex')}`);
            const newFormat = data[0] === 0xff;
            // In the new format, the second byte contains the number of inputs to watch
            const inputs = new Set<number>();
            if (newFormat) {
                for (let i = 0; i < data[1]; i++) {
                    inputs.add(data[i + 2]);
                }
                console.log(`Watching inputs (new format) ${Array.from(inputs).join(', ')}`);
                newFormatSockets.add(socket);
            } else {
                // In the old format, the first byte is the one and only input to watch
                const mappedInput = legacyMap.get(data[0]);
                if (mappedInput) {
                    inputs.add(mappedInput);
                    console.log(`Watching input ${mappedInput} (mapped from ${data[0]})`);
                } else {
                    inputs.add(data[0]);
                    console.log(`Watching input ${data[0]} (unmapped)`);
                }
            }
            usedInputs.set(clientIdent, inputs);
            inputs.forEach((input) => {
                if (!socketSets.has(input)) {
                    socketSets.set(input, new Set());
                    // Obtain and remember initial state
                    lastStates.set(input, getState(input));
                }
                socketSets.get(input)?.add(socket);
                sendState(input, lastStates.get(input)!, socket);
            });
            // Register echo-handler for keep-alive replies
            socket.on('data', (data) => socket.write(data));
        });

        socket.on('error', (err) => {
            console.error(err);
        });

        // Add a 'close' event handler to this instance of socket
        socket.on('close', (hadError) => {
            if (newFormatSockets.has(socket)) {
                newFormatSockets.delete(socket);
            }
            const inputs = usedInputs.get(clientIdent);
            usedInputs.delete(clientIdent);
            if (inputs) {
                inputs.forEach((input) => {
                    socketSets.get(input)?.delete(socket);
                    // Cleanup input watching when no more sockets left
                    if (socketSets.get(input)?.size === 0) {
                        socketSets.delete(input);
                        lastStates.delete(input);
                        console.log(`Stop watching input # ${input}, no client sockets left.`);
                    }
                });
            }
            console.log(`CLOSED: ${clientIdent} (${hadError ? 'with error' : 'clean'})`);
        });
    }).listen(PORT, '0.0.0.0', () => {
        console.log(`TCP Server is listening on port ${PORT}.`);
    });
});

// Do IP lookup and connect to ATEM
doLookup();
