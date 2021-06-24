import { TallyState } from "./tallyState";
import { Atem } from "atem-connection";
import { createServer, Socket } from "net";
import { lookup } from "dns";

const PORT = 7411;
const ATEM_DNS = 'atem.mk';

const atem = new Atem();
const lastStates = new Map<number, TallyState>();
const socketSets = new Map<number, Set<Socket>>();
const usedInputs = new Map<string, number>();

atem.on('error', console.error);
// Manually resolve IP address to prevent empic reconnect fuckup of atem-connect
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
doLookup();

const sendState = (state: TallyState, socket: Socket) => {
    try {
        const stateData = Uint8Array.from([state]);
        socket.write(stateData);
    } catch(e) {
        console.error(e);
    }
};

const getState = (input: number): TallyState => {
    const previewSet = new Set(atem.listVisibleInputs("preview"));
    const programSet = new Set(atem.listVisibleInputs("program"));
    if (programSet.has(input)) {
        return TallyState.PROGRAM;
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
            socketSets.get(cam)?.forEach((socket) => sendState(state, socket));
        }
    };
    for (const cam of Array.from(socketSets.keys())) {
        if (programSet.has(cam)) {
            handleState(cam, TallyState.PROGRAM, "Program State");
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
        const clientIdent = `${socket.remoteAddress} + ':' + ${socket.remotePort}`;
        console.log(`CONNECTED: ${clientIdent}`);

        socket.once('data', function (data) {
            console.log(`DATA on ${clientIdent}: ${data[0]}`);
            const input = data[0];
            if (input > 10) {
                throw new Error(`Invalid input # ${input}`);
            } else {
                console.log(`Watching input ${input}`);
                usedInputs.set(clientIdent, input);
                if (!socketSets.has(input)) {
                    socketSets.set(input, new Set());
                    // Obtain and remember initial state
                    lastStates.set(input, getState(input));
                }
                socketSets.get(input)?.add(socket);
                sendState(lastStates.get(input) ?? TallyState.INACTIVE, socket);
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
                socketSets.get(input)?.delete(socket);
                // Cleanup
                if (socketSets.get(input)?.size === 0) {
                    socketSets.delete(input);
                    lastStates.delete(input);
                    console.log(`Stop watching input # ${input}, no client sockets left.`);
                }
            }
            console.log(`CLOSED: ${clientIdent} (${hadError ? 'with error' : 'clean'})`);
        });
    }).listen(PORT, '0.0.0.0', () => {
        console.log(`TCP Server is listening on port ${PORT}.`);
    });
});
 